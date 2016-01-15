// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/edk/system/message_pipe_dispatcher.h"

#include <limits>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "mojo/edk/embedder/embedder_internal.h"
#include "mojo/edk/system/core.h"
#include "mojo/edk/system/node_controller.h"
#include "mojo/edk/system/ports_message.h"
#include "mojo/public/c/system/macros.h"

namespace mojo {
namespace edk {

namespace {

// Header attached to every message sent over a message pipe.
struct MOJO_ALIGNAS(8) MessageHeader {
  // The number of serialized dispatchers included in this header.
  uint32_t num_dispatchers;

  // Total size of the header, including serialized dispatcher data.
  uint32_t header_size;
};

// Header for each dispatcher, immediately following the message header.
struct MOJO_ALIGNAS(8) DispatcherHeader {
  // The type of the dispatcher, correpsonding to the Dispatcher::Type enum.
  int32_t type;

  // The size of the serialized dispatcher, not including this header.
  uint32_t num_bytes;

  // The number of ports needed to deserialize this dispatcher.
  uint32_t num_ports;

  // The number of platform handles needed to deserialize this dispatcher.
  uint32_t num_platform_handles;
};

}  // namespace

// A PortObserver which forwards to a MessagePipeDispatcher. This owns a
// reference to the MPD to ensure it lives as long as the observed port.
class MessagePipeDispatcher::PortObserverThunk
    : public NodeController::PortObserver {
 public:
  explicit PortObserverThunk(scoped_refptr<MessagePipeDispatcher> dispatcher)
      : dispatcher_(dispatcher) {}

 private:
  ~PortObserverThunk() override {}

  // NodeController::PortObserver:
  void OnPortStatusChanged() override { dispatcher_->OnPortStatusChanged(); }

  scoped_refptr<MessagePipeDispatcher> dispatcher_;

  DISALLOW_COPY_AND_ASSIGN(PortObserverThunk);
};

MessagePipeDispatcher::MessagePipeDispatcher(NodeController* node_controller,
                                             const ports::PortRef& port,
                                             bool connected)
    : node_controller_(node_controller),
      port_(port),
      port_connected_(connected) {
  DVLOG(2) << "Creating new MessagePipeDispatcher for port " << port.name()
           << " [connected=" << connected << "]";

  // OnPortStatusChanged (via PortObserverThunk) may be called before this
  // constructor returns. Hold a lock here to prevent signal races.
  base::AutoLock lock(signal_lock_);
  node_controller_->SetPortObserver(
      port_,
      make_scoped_refptr(new PortObserverThunk(this)));
}

Dispatcher::Type MessagePipeDispatcher::GetType() const {
  return Type::MESSAGE_PIPE;
}

MojoResult MessagePipeDispatcher::Close() {
  base::AutoLock lock(signal_lock_);
  return CloseNoLock();
}

MojoResult MessagePipeDispatcher::WriteMessage(
    const void* bytes,
    uint32_t num_bytes,
    const DispatcherInTransit* dispatchers,
    uint32_t num_dispatchers,
    MojoWriteMessageFlags flags) {
  {
    base::AutoLock lock(signal_lock_);
    if (port_closed_ || in_transit_)
      return MOJO_RESULT_INVALID_ARGUMENT;
  }

  struct DispatcherInfo {
    uint32_t num_bytes;
    uint32_t num_ports;
    uint32_t num_handles;
  };

  size_t header_size = sizeof(MessageHeader) +
      num_dispatchers * sizeof(DispatcherHeader);
  size_t num_ports = 0;

  std::vector<DispatcherInfo> dispatcher_info(num_dispatchers);
  for (size_t i = 0; i < num_dispatchers; ++i) {
    Dispatcher* d = dispatchers[i].dispatcher.get();
    d->StartSerialize(&dispatcher_info[i].num_bytes,
                      &dispatcher_info[i].num_ports,
                      &dispatcher_info[i].num_handles);
    header_size += dispatcher_info[i].num_bytes;
    num_ports += dispatcher_info[i].num_ports;
  }

  scoped_ptr<PortsMessage> message =
      node_controller_->AllocMessage(header_size + num_bytes, num_ports);
  DCHECK(message);

  // Populate the message header with information about serialized dispatchers.

  MessageHeader* header =
      static_cast<MessageHeader*>(message->mutable_payload_bytes());
  DispatcherHeader* dispatcher_headers =
      reinterpret_cast<DispatcherHeader*>(reinterpret_cast<char*>(header) +
                                          sizeof(MessageHeader));
  void* dispatcher_data = &dispatcher_headers[num_dispatchers];

  header->num_dispatchers = num_dispatchers;

  DCHECK_LE(header_size, std::numeric_limits<uint32_t>::max());
  header->header_size = static_cast<uint32_t>(header_size);

  bool cancel_transit = false;
  if (num_dispatchers > 0) {
    ScopedPlatformHandleVectorPtr handles(new PlatformHandleVector);
    size_t port_index = 0;
    for (size_t i = 0; i < num_dispatchers; ++i) {
      Dispatcher* d = dispatchers[i].dispatcher.get();

      DispatcherHeader* dh = &dispatcher_headers[i];
      dh->type = static_cast<int32_t>(d->GetType());
      dh->num_bytes = dispatcher_info[i].num_bytes;
      dh->num_ports = dispatcher_info[i].num_ports;
      dh->num_platform_handles = dispatcher_info[i].num_handles;

      std::vector<ports::PortName> ports(dispatcher_info[i].num_ports);
      if (!d->EndSerialize(dispatcher_data, ports.data(), handles.get())) {
        cancel_transit = true;
        break;
      }

      if (!ports.empty()) {
        std::copy(ports.begin(), ports.end(),
                  message->mutable_ports() + port_index);
        port_index += ports.size();
      }

      dispatcher_data = static_cast<void*>(
          static_cast<char*>(dispatcher_data) + dh->num_bytes);
    }

    if (!cancel_transit) {
      message->SetHandles(std::move(handles));
    } else {
      // Release any platform handles we've accumulated. Their dispatchers
      // retain ownership when transit is canceled, so these are not actually
      // leaking.
      handles->clear();
    }
  }

  MojoResult result = MOJO_RESULT_OK;
  if (!cancel_transit) {
    // Copy the message body.
    void* message_body = static_cast<void*>(
        static_cast<char*>(message->mutable_payload_bytes()) + header_size);
    memcpy(message_body, bytes, num_bytes);

    int rv = node_controller_->SendMessage(port_, &message);
    if (rv != ports::OK) {
      base::AutoLock lock(signal_lock_);
      if (rv == ports::ERROR_PORT_UNKNOWN ||
          rv == ports::ERROR_PORT_STATE_UNEXPECTED ||
          rv == ports::ERROR_PORT_CANNOT_SEND_PEER) {
        result = MOJO_RESULT_INVALID_ARGUMENT;
      } else if (rv == ports::ERROR_PORT_PEER_CLOSED) {
        awakables_.AwakeForStateChange(GetHandleSignalsStateNoLock());
        result = MOJO_RESULT_FAILED_PRECONDITION;
      } else {
        NOTREACHED();
        result = MOJO_RESULT_UNKNOWN;
      }
      cancel_transit = true;
    } else {
      DCHECK(!message);
    }
  }

  if (cancel_transit) {
    // We ended up not sending the message. Release all the platform handles.
    // Their dipatchers retain ownership when transit is canceled, so these are
    // not actually leaking.
    DCHECK(message);
    Channel::MessagePtr m = message->TakeChannelMessage();
    ScopedPlatformHandleVectorPtr handles = m->TakeHandles();
    if (handles)
      handles->clear();
  }

  return result;
}

MojoResult MessagePipeDispatcher::ReadMessage(void* bytes,
                                              uint32_t* num_bytes,
                                              MojoHandle* handles,
                                              uint32_t* num_handles,
                                              MojoReadMessageFlags flags) {
  {
    base::AutoLock lock(signal_lock_);
    if (port_closed_ || in_transit_)
      return MOJO_RESULT_INVALID_ARGUMENT;

    if (!port_connected_)
      return MOJO_RESULT_SHOULD_WAIT;
  }

  bool no_space = false;
  bool may_discard = flags & MOJO_READ_MESSAGE_FLAG_MAY_DISCARD;

  // Ensure the provided buffers are large enough to hold the next message.
  // GetMessageIf provides an atomic way to test the next message without
  // committing to removing it from the port's underlying message queue until
  // we are sure we can consume it.

  ports::ScopedMessage ports_message;
  int rv = node_controller_->node()->GetMessageIf(
      port_,
      [num_bytes, num_handles, &no_space, &may_discard](
          const ports::Message& next_message) {
        const PortsMessage& message =
            static_cast<const PortsMessage&>(next_message);
        DCHECK_GE(message.num_payload_bytes(), sizeof(MessageHeader));
        const MessageHeader* header =
            static_cast<const MessageHeader*>(message.payload_bytes());
        DCHECK_LE(header->header_size, message.num_payload_bytes());

        uint32_t bytes_to_read = 0;
        uint32_t bytes_available =
            static_cast<uint32_t>(message.num_payload_bytes()) -
            header->header_size;
        if (num_bytes) {
          bytes_to_read = std::min(*num_bytes, bytes_available);
          *num_bytes = bytes_available;
        }

        DCHECK_EQ(header->num_dispatchers,
                  message.num_ports() + message.num_handles());

        uint32_t handles_to_read = 0;
        uint32_t handles_available = header->num_dispatchers;
        if (num_handles) {
          handles_to_read = std::min(*num_handles, handles_available);
          *num_handles = handles_available;
        }

        if (bytes_to_read < bytes_available ||
            handles_to_read < handles_available) {
          no_space = true;
          return may_discard;
        }

        return true;
      },
      &ports_message);

  if (rv != ports::OK && rv != ports::ERROR_PORT_PEER_CLOSED) {
    if (rv == ports::ERROR_PORT_UNKNOWN ||
        rv == ports::ERROR_PORT_STATE_UNEXPECTED)
      return MOJO_RESULT_INVALID_ARGUMENT;

    NOTREACHED();
    return MOJO_RESULT_UNKNOWN;  // TODO: Add a better error code here?
  }

  if (no_space)
    return MOJO_RESULT_RESOURCE_EXHAUSTED;

  if (!ports_message) {
    if (rv == ports::OK)
      return MOJO_RESULT_SHOULD_WAIT;

    // Peer is closed and there are no more messages to read.
    DCHECK_EQ(rv, ports::ERROR_PORT_PEER_CLOSED);
    base::AutoLock lock(signal_lock_);
    awakables_.AwakeForStateChange(GetHandleSignalsStateNoLock());
    return MOJO_RESULT_FAILED_PRECONDITION;
  }

  scoped_ptr<PortsMessage> message(
      static_cast<PortsMessage*>(ports_message.release()));
  const MessageHeader* header =
      static_cast<const MessageHeader*>(message->payload_bytes());
  const DispatcherHeader* dispatcher_headers =
      reinterpret_cast<const DispatcherHeader*>(
          reinterpret_cast<const char*>(header) + sizeof(MessageHeader));
  size_t header_size = sizeof(MessageHeader) +
      header->num_dispatchers * sizeof(DispatcherHeader);
  DCHECK_GE(message->num_payload_bytes(), header_size);
  DCHECK_EQ(message->num_ports() + message->num_handles(),
            header->num_dispatchers);

  const void* dispatcher_data = &dispatcher_headers[header->num_dispatchers];

  // Deserialize dispatchers.
  if (header->num_dispatchers > 0) {
    CHECK(handles);
    std::vector<DispatcherInTransit> dispatchers(header->num_dispatchers);
    size_t port_index = 0;
    size_t platform_handle_index = 0;
    for (size_t i = 0; i < header->num_dispatchers; ++i) {
      const DispatcherHeader& dh = dispatcher_headers[i];
      Type type = static_cast<Type>(dh.type);

      DCHECK_GE(message->num_ports(),
                port_index + dh.num_ports);
      DCHECK_GE(message->num_handles(),
                platform_handle_index + dh.num_platform_handles);

      PlatformHandle* out_handles =
          message->num_handles() ? message->handles() + platform_handle_index
                                 : nullptr;
      dispatchers[i].dispatcher = Dispatcher::Deserialize(
          type, dispatcher_data, dh.num_bytes, message->ports() + port_index,
          dh.num_ports, out_handles, dh.num_platform_handles);
      if (!dispatchers[i].dispatcher)
        return MOJO_RESULT_UNKNOWN;

      header_size += dh.num_bytes;
      port_index += dh.num_ports;
      platform_handle_index += dh.num_platform_handles;
    }

    if (!node_controller_->core()->AddDispatchersFromTransit(dispatchers,
                                                             handles))
      return MOJO_RESULT_UNKNOWN;
  }

  // Copy message bytes.
  DCHECK_GE(message->num_payload_bytes(), header_size);
  const void* message_body = static_cast<const void*>(
      reinterpret_cast<const char*>(message->payload_bytes()) + header_size);
  memcpy(bytes, message_body, message->num_payload_bytes() - header_size);

  return MOJO_RESULT_OK;
}

HandleSignalsState
MessagePipeDispatcher::GetHandleSignalsState() const {
  base::AutoLock lock(signal_lock_);
  return GetHandleSignalsStateNoLock();
}

MojoResult MessagePipeDispatcher::AddAwakable(
    Awakable* awakable,
    MojoHandleSignals signals,
    uintptr_t context,
    HandleSignalsState* signals_state) {
  base::AutoLock lock(signal_lock_);

  if (port_closed_ || in_transit_) {
    if (signals_state)
      *signals_state = HandleSignalsState();
    return MOJO_RESULT_INVALID_ARGUMENT;
  }

  HandleSignalsState state = GetHandleSignalsStateNoLock();
  if (state.satisfies(signals)) {
    if (signals_state)
      *signals_state = state;
    return MOJO_RESULT_ALREADY_EXISTS;
  }
  if (!state.can_satisfy(signals)) {
    if (signals_state)
      *signals_state = state;
    return MOJO_RESULT_FAILED_PRECONDITION;
  }

  awakables_.Add(awakable, signals, context);
  return MOJO_RESULT_OK;
}

void MessagePipeDispatcher::RemoveAwakable(Awakable* awakable,
                                           HandleSignalsState* signals_state) {
  base::AutoLock lock(signal_lock_);
  if (port_closed_ || in_transit_) {
    if (signals_state)
      *signals_state = HandleSignalsState();
  } else if (signals_state) {
    *signals_state = GetHandleSignalsStateNoLock();
  }
  awakables_.Remove(awakable);
}

void MessagePipeDispatcher::StartSerialize(uint32_t* num_bytes,
                                           uint32_t* num_ports,
                                           uint32_t* num_handles) {
  *num_bytes = 0;
  *num_ports = 1;
  *num_handles = 0;
}

bool MessagePipeDispatcher::EndSerialize(void* destination,
                                         ports::PortName* ports,
                                         PlatformHandleVector* handles) {
  ports[0] = port_.name();
  return true;
}

bool MessagePipeDispatcher::BeginTransit() {
  base::AutoLock lock(signal_lock_);
  if (in_transit_)
    return false;
  in_transit_ = port_connected_;
  return in_transit_;
}

void MessagePipeDispatcher::CompleteTransitAndClose() {
  base::AutoLock lock(signal_lock_);
  in_transit_ = false;
  port_transferred_ = true;
  CloseNoLock();
}

void MessagePipeDispatcher::CancelTransit() {
  base::AutoLock lock(signal_lock_);
  in_transit_ = false;

  // Something may have happened while we were waiting for potential transit.
  awakables_.AwakeForStateChange(GetHandleSignalsStateNoLock());
}

// static
scoped_refptr<Dispatcher> MessagePipeDispatcher::Deserialize(
    const void* data,
    size_t num_bytes,
    const ports::PortName* ports,
    size_t num_ports,
    PlatformHandle* handles,
    size_t num_handles) {
  if (num_ports != 1 || num_handles || num_bytes)
    return nullptr;

  ports::PortRef port;
  CHECK_EQ(
      ports::OK,
      internal::g_core->GetNodeController()->node()->GetPort(ports[0], &port));

  // Note: disconnected ports cannot be serialized.
  return new MessagePipeDispatcher(internal::g_core->GetNodeController(), port,
                                   true /* connected */);
}

MessagePipeDispatcher::~MessagePipeDispatcher() {
  DCHECK(port_closed_ && !in_transit_);
}

MojoResult MessagePipeDispatcher::CloseNoLock() {
  signal_lock_.AssertAcquired();
  if (port_closed_ || in_transit_)
    return MOJO_RESULT_INVALID_ARGUMENT;

  port_closed_ = true;
  awakables_.CancelAll();

  if (!port_transferred_ && port_connected_) {
    int rv = node_controller_->node()->ClosePort(port_);
    DCHECK_EQ(ports::OK, rv);
  }

  return MOJO_RESULT_OK;
}

HandleSignalsState MessagePipeDispatcher::GetHandleSignalsStateNoLock() const {
  HandleSignalsState rv;
  if (!port_connected_) {
    // If we aren't connected yet, treat the pipe like it's in a normal
    // state with no messages available.
    rv.satisfiable_signals = MOJO_HANDLE_SIGNAL_READABLE |
        MOJO_HANDLE_SIGNAL_WRITABLE | MOJO_HANDLE_SIGNAL_PEER_CLOSED;
    rv.satisfied_signals = MOJO_HANDLE_SIGNAL_WRITABLE;
    return rv;
  }

  ports::PortStatus port_status;
  if (node_controller_->node()->GetStatus(port_, &port_status) != ports::OK) {
    CHECK(in_transit_ || port_transferred_ || port_closed_);
    return HandleSignalsState();
  }

  if (port_status.has_messages) {
    rv.satisfied_signals |= MOJO_HANDLE_SIGNAL_READABLE;
    rv.satisfiable_signals |= MOJO_HANDLE_SIGNAL_READABLE;
  }
  if (!port_status.peer_closed) {
    rv.satisfied_signals |= MOJO_HANDLE_SIGNAL_WRITABLE;
    rv.satisfiable_signals |= MOJO_HANDLE_SIGNAL_WRITABLE;
    rv.satisfiable_signals |= MOJO_HANDLE_SIGNAL_READABLE;
  } else {
    rv.satisfied_signals |= MOJO_HANDLE_SIGNAL_PEER_CLOSED;
  }
  rv.satisfiable_signals |= MOJO_HANDLE_SIGNAL_PEER_CLOSED;
  return rv;
}

void MessagePipeDispatcher::OnPortStatusChanged() {
  base::AutoLock lock(signal_lock_);
  if (!port_connected_) {
    port_connected_ = true;
    if (port_closed_) {
      int rv = node_controller_->node()->ClosePort(port_);
      DCHECK_EQ(rv, ports::OK);
    }
  }
  awakables_.AwakeForStateChange(GetHandleSignalsStateNoLock());
}

}  // namespace edk
}  // namespace mojo
