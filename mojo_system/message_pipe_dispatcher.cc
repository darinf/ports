// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ports/mojo_system/message_pipe_dispatcher.h"

#include <limits>

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "mojo/public/c/system/macros.h"
#include "ports/mojo_system/core.h"
#include "ports/mojo_system/node.h"
#include "ports/mojo_system/ports_message.h"

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

  // The number of platform handles needed to deserialize this dispatcher.
  uint32_t num_platform_handles;
};

}  // namespace

// A PortObserver which forwards to a MessagePipeDispatcher. This owns a
// reference to the MPD to ensure it lives as long as the observed port.
class MessagePipeDispatcher::PortObserverThunk : public Node::PortObserver {
 public:
  explicit PortObserverThunk(scoped_refptr<MessagePipeDispatcher> dispatcher)
      : dispatcher_(dispatcher) {}
  ~PortObserverThunk() override {}

 private:
  // Node::PortObserver:
  void OnMessagesAvailable() override { dispatcher_->OnMessagesAvailable(); }

  scoped_refptr<MessagePipeDispatcher> dispatcher_;

  DISALLOW_COPY_AND_ASSIGN(PortObserverThunk);
};

MessagePipeDispatcher::MessagePipeDispatcher(Node* node,
                                             const ports::PortRef& port)
    : node_(node), port_(port) {
  // OnMessagesAvailable (via PortObserverThunk) may be called before this
  // constructor returns. Hold a lock here to prevent signal races.
  base::AutoLock lock(signal_lock_);
  node_->SetPortObserver(port_, std::make_shared<PortObserverThunk>(this));
}

Dispatcher::Type MessagePipeDispatcher::GetType() const {
  return Type::MESSAGE_PIPE;
}

void MessagePipeDispatcher::Close() {
  {
    base::AutoLock lock(signal_lock_);
    DCHECK(!port_closed_);
    port_closed_ = true;
    if (!port_transferred_)
      node_->ClosePort(port_);
  }

  {
    base::AutoLock lock(awakables_lock_);
    awakables_.CancelAll();
  }
}

MojoResult MessagePipeDispatcher::WriteMessage(
    const void* bytes,
    uint32_t num_bytes,
    const DispatcherInTransit* dispatchers,
    uint32_t num_dispatchers,
    MojoWriteMessageFlags flags) {
  size_t header_size = sizeof(MessageHeader) +
      num_dispatchers * sizeof(DispatcherHeader);
  size_t num_ports = 0;
  for (size_t i = 0; i < num_dispatchers; ++i) {
    Dispatcher* d = dispatchers[i].dispatcher.get();
    if (d->GetType() == Type::MESSAGE_PIPE)
      num_ports++;
    uint32_t dispatcher_size = 0;
    uint32_t num_handles = 0;
    d->GetSerializedSize(&dispatcher_size, &num_handles);
    header_size += dispatcher_size;
  }

  scoped_ptr<PortsMessage> message =
      node_->AllocMessage(header_size + num_bytes, num_ports);
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

  if (num_dispatchers > 0) {
    ScopedPlatformHandleVectorPtr handles(new PlatformHandleVector);
    size_t port_index = 0;
    for (size_t i = 0; i < num_dispatchers; ++i) {
      Dispatcher* d = dispatchers[i].dispatcher.get();
      if (d->GetType() == Type::MESSAGE_PIPE) {
        MessagePipeDispatcher* mpd = static_cast<MessagePipeDispatcher*>(d);
        message->mutable_ports()[port_index] = mpd->GetPortName();
        port_index++;
      }

      DispatcherHeader* dh = &dispatcher_headers[i];
      dh->type = static_cast<int32_t>(d->GetType());
      d->GetSerializedSize(&dh->num_bytes, &dh->num_platform_handles);
      if (!d->SerializeAndClose(dispatcher_data, handles.get())) {
        // TODO: fail in a more useful manner?
        LOG(ERROR) << "Failed to serialize dispatcher.";
      }
      dispatcher_data = static_cast<void*>(
          static_cast<char*>(dispatcher_data) + dh->num_bytes);
    }

    message->SetHandles(std::move(handles));
  }

  // Copy the message body.
  void* message_body = static_cast<void*>(
      static_cast<char*>(message->mutable_payload_bytes()) + header_size);
  memcpy(message_body, bytes, num_bytes);

  int rv = node_->SendMessage(port_, std::move(message));

  if (rv != ports::OK) {
    if (rv == ports::ERROR_PORT_UNKNOWN ||
        rv == ports::ERROR_PORT_STATE_UNEXPECTED)
      return MOJO_RESULT_INVALID_ARGUMENT;

    if (rv == ports::ERROR_PORT_PEER_CLOSED) {
      {
        base::AutoLock lock(signal_lock_);
        peer_closed_ = true;
      }

      base::AutoLock lock(awakables_lock_);
      awakables_.AwakeForStateChange(GetHandleSignalsState());
      return MOJO_RESULT_FAILED_PRECONDITION;
    }

    NOTREACHED();
    return MOJO_RESULT_UNKNOWN;
  }

  return MOJO_RESULT_OK;
}

MojoResult MessagePipeDispatcher::ReadMessage(void* bytes,
                                              uint32_t* num_bytes,
                                              MojoHandle* handles,
                                              uint32_t* num_handles,
                                              MojoReadMessageFlags flags) {
  bool no_space = false;

  // Ensure the provided buffers are large enough to hold the next message.
  // GetMessageIf provides an atomic way to test the next message without
  // committing to removing it from the port's underlying message queue until
  // we are sure we can consume it.

  ports::ScopedMessage ports_message;
  int rv = node_->GetMessageIf(
      port_,
      [num_bytes, num_handles, &no_space](const ports::Message& next_message) {
        const PortsMessage& message =
            static_cast<const PortsMessage&>(next_message);
        DCHECK_GE(message.num_payload_bytes(), sizeof(MessageHeader));
        const MessageHeader* header =
            static_cast<const MessageHeader*>(message.payload_bytes());
        DCHECK_LE(header->header_size, message.num_payload_bytes());

        size_t bytes_to_read = 0;
        size_t bytes_available =
            message.num_payload_bytes() - header->header_size;
        if (num_bytes) {
          bytes_to_read = std::min(static_cast<size_t>(*num_bytes),
                                   bytes_available);
          *num_bytes = bytes_available;
        }

        DCHECK_EQ(header->num_dispatchers,
                  message.num_ports() + message.num_handles());

        size_t handles_to_read = 0;
        size_t handles_available = header->num_dispatchers;
        if (num_handles) {
          handles_to_read = std::min(static_cast<size_t>(*num_handles),
                                     handles_available);
          *num_handles = handles_available;
        }

        if (bytes_to_read < bytes_available ||
            handles_to_read < handles_available) {
          no_space = true;
          return false;
        }

        return true;
      },
      &ports_message);

  if (rv != ports::OK) {
    if (rv == ports::ERROR_PORT_UNKNOWN ||
        rv == ports::ERROR_PORT_STATE_UNEXPECTED)
      return MOJO_RESULT_INVALID_ARGUMENT;

    if (rv == ports::ERROR_PORT_PEER_CLOSED) {
      {
        base::AutoLock lock(signal_lock_);
        peer_closed_ = true;
      }

      base::AutoLock lock(awakables_lock_);
      awakables_.AwakeForStateChange(GetHandleSignalsState());
      return MOJO_RESULT_FAILED_PRECONDITION;
    }

    NOTREACHED();
    return MOJO_RESULT_UNKNOWN;  // TODO: Add a better error code here?
  }

  if (no_space)
    return MOJO_RESULT_RESOURCE_EXHAUSTED;

  if (!ports_message) {
    base::AutoLock lock(signal_lock_);
    port_readable_ = false;
    return MOJO_RESULT_SHOULD_WAIT;
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

  // Deserialize pipes as new ports.
  std::vector<MojoHandle> pipe_handles(message->num_ports());
  if (!node_->core()->AddDispatchersForReceivedPorts(*message,
                                                     pipe_handles.data())) {
    // TODO: Close all of the received ports.
    return MOJO_RESULT_UNKNOWN;  // TODO: Add a better error code here?
  }

  // Deserialize all other types of dispatchers.
  std::vector<MojoHandle> other_handles;
  if (header->num_dispatchers > 0) {
    std::vector<DispatcherInTransit> dispatchers(
        header->num_dispatchers - message->num_ports());
    size_t platform_handle_index = 0;
    for (size_t i = 0, dispatcher_index = 0; i < header->num_dispatchers; ++i) {
      const DispatcherHeader& dh = dispatcher_headers[i];
      Type type = static_cast<Type>(dh.type);
      if (type == Type::MESSAGE_PIPE) {
        // Pipes have already been deserialized as ports above.
        continue;
      }

      DCHECK_GE(message->num_handles(),
                platform_handle_index + dh.num_platform_handles);
      dispatchers[dispatcher_index].dispatcher = Dispatcher::Deserialize(
          type, dispatcher_data, dh.num_bytes,
          &(message->handles()[platform_handle_index]),
          dh.num_platform_handles);
      if (!dispatchers[dispatcher_index].dispatcher)
        return MOJO_RESULT_UNKNOWN;
      header_size += dh.num_bytes;
      platform_handle_index += dh.num_platform_handles;
      dispatcher_index++;
    }

    other_handles.resize(dispatchers.size());
    if (!node_->core()->AddDispatchersFromTransit(dispatchers,
                                                  other_handles.data())) {
      return MOJO_RESULT_UNKNOWN;
    }
  }

  // Copy handles from all the new dispatchers.
  size_t pipe_handle_index = 0;
  size_t other_handle_index = 0;
  for (size_t i = 0; i < header->num_dispatchers; ++i) {
    const DispatcherHeader& dh = dispatcher_headers[i];
    if (static_cast<Type>(dh.type) == Type::MESSAGE_PIPE) {
      DCHECK_LT(pipe_handle_index, pipe_handles.size());
      handles[i] = pipe_handles[pipe_handle_index++];
    } else {
      DCHECK_LT(other_handle_index, other_handles.size());
      handles[i] = other_handles[other_handle_index++];
    }
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
  HandleSignalsState rv;
  if (port_readable_) {
    rv.satisfied_signals |= MOJO_HANDLE_SIGNAL_READABLE;
    rv.satisfiable_signals |= MOJO_HANDLE_SIGNAL_READABLE;
  }
  if (!peer_closed_) {
    rv.satisfied_signals |= MOJO_HANDLE_SIGNAL_WRITABLE;
    rv.satisfiable_signals |= MOJO_HANDLE_SIGNAL_READABLE;
    rv.satisfiable_signals |= MOJO_HANDLE_SIGNAL_WRITABLE;
  } else {
    rv.satisfied_signals |= MOJO_HANDLE_SIGNAL_PEER_CLOSED;
  }
  rv.satisfiable_signals |= MOJO_HANDLE_SIGNAL_PEER_CLOSED;
  return rv;
}

MojoResult MessagePipeDispatcher::AddAwakable(
    Awakable* awakable,
    MojoHandleSignals signals,
    uintptr_t context,
    HandleSignalsState* signals_state) {
  UpdateSignalsState();
  HandleSignalsState state = GetHandleSignalsState();
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

  base::AutoLock locker(awakables_lock_);
  awakables_.Add(awakable, signals, context);
  return MOJO_RESULT_OK;
}

void MessagePipeDispatcher::RemoveAwakable(Awakable* awakable,
                                           HandleSignalsState* signals_state) {
  base::AutoLock locker(awakables_lock_);
  awakables_.Remove(awakable);
}

void MessagePipeDispatcher::GetSerializedSize(uint32_t* num_bytes,
                                              uint32_t* num_handles) {
  *num_bytes = 0;
  *num_handles = 0;
}

bool MessagePipeDispatcher::SerializeAndClose(void* destination,
                                              PlatformHandleVector* handles) {
  // Nothing to do. Pipes are serialied as ports.
  return true;
}

void MessagePipeDispatcher::CompleteTransit() {
  {
    base::AutoLock lock(signal_lock_);
    port_transferred_ = true;
  }

  // port_ has been closed by virtue of having been transferred. This
  // dispatcher needs to be closed as well.
  Close();
}

MessagePipeDispatcher::~MessagePipeDispatcher() {
}

bool MessagePipeDispatcher::UpdateSignalsState() {
  // Peek at the queue. If our selector function runs at all, it's not empty.
  //
  // TODO: maybe Node should have an interface for this test?
  bool has_messages = false;
  ports::ScopedMessage message;
  int rv = node_->GetMessageIf(
      port_,
      [&has_messages](const ports::Message&) {
        has_messages = true;
        return false;  // Don't return the message.
      },
      &message);

  DCHECK(rv == ports::OK || ports::ERROR_PORT_PEER_CLOSED);

  // Awakables will not be interested in this becoming unreadable.
  bool awakable_change = false;

  base::AutoLock lock(signal_lock_);
  if (rv == ports::OK) {
    if (has_messages && !port_readable_)
      awakable_change = true;
    port_readable_ = has_messages;
  }

  if (rv == ports::ERROR_PORT_PEER_CLOSED && !peer_closed_) {
    peer_closed_ = true;
    awakable_change = true;
  }

  return awakable_change;
}

void MessagePipeDispatcher::OnMessagesAvailable() {
  if (UpdateSignalsState()) {
    HandleSignalsState state = GetHandleSignalsState();

    base::AutoLock locker(awakables_lock_);
    awakables_.AwakeForStateChange(state);
  }
}

}  // namespace edk
}  // namespace mojo
