// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ports/mojo_system/message_pipe_dispatcher.h"

#include "base/macros.h"
#include "ports/mojo_system/core.h"
#include "ports/mojo_system/node.h"

namespace mojo {
namespace edk {

// A PortObserver which forwards to a MessagePipeDispatcher. This owns a
// reference to the MPD to ensure it lives as long as the observed port.
class MessagePipeDispatcher::LocalPortObserver : public Node::PortObserver {
 public:
  explicit LocalPortObserver(scoped_refptr<MessagePipeDispatcher> dispatcher)
      : dispatcher_(dispatcher) {}
  ~LocalPortObserver() override {}

 private:
  // Node::PortObserver:
  void OnMessagesAvailable() override { dispatcher_->OnMessagesAvailable(); }

  scoped_refptr<MessagePipeDispatcher> dispatcher_;

  DISALLOW_COPY_AND_ASSIGN(LocalPortObserver);
};

MessagePipeDispatcher::MessagePipeDispatcher(Node* node,
                                             const ports::PortName& port_name)
    : node_(node), port_name_(port_name) {
  // OnMessagesAvailable (via LocalPortObserver) may be called before this
  // constructor returns. Hold a lock here to prevent signal races.
  base::AutoLock dispatcher_lock(lock());
  node_->SetPortObserver(
      port_name_, make_scoped_ptr(new LocalPortObserver(this)));
  if (HasMessagesQueuedNoLock()) {
    port_readable_ = true;
  }
}

Dispatcher::Type MessagePipeDispatcher::GetType() const {
  return Type::MESSAGE_PIPE;
}

MessagePipeDispatcher::~MessagePipeDispatcher() {
}

void MessagePipeDispatcher::CloseImplNoLock() {
  lock().AssertAcquired();
  DCHECK(is_closed());
  if (!port_transferred_)
    node_->ClosePort(port_name_);
}

MojoResult MessagePipeDispatcher::WriteMessageImplNoLock(
    const void* bytes,
    uint32_t num_bytes,
    const DispatcherInTransit* dispatchers,
    uint32_t num_dispatchers,
    MojoWriteMessageFlags flags) {
  lock().AssertAcquired();

  ports::ScopedMessage message(ports::AllocMessage(num_bytes, num_dispatchers));
  memcpy(message->bytes, bytes, num_bytes);
  for (size_t i = 0; i < num_dispatchers; ++i) {
    Dispatcher* d = dispatchers[i].dispatcher.get();

    // TODO: support transferring other types of handles
    CHECK_EQ(d->GetType(), Type::MESSAGE_PIPE);

    MessagePipeDispatcher* mpd = static_cast<MessagePipeDispatcher*>(d);
    message->ports[i].name = mpd->GetPortName();
  }

  int rv = node_->SendMessage(port_name_, std::move(message));

  if (rv != ports::OK) {
    if (rv == ports::ERROR_PORT_UNKNOWN ||
        rv == ports::ERROR_PORT_STATE_UNEXPECTED)
      return MOJO_RESULT_INVALID_ARGUMENT;

    if (rv == ports::ERROR_PORT_PEER_CLOSED) {
      peer_closed_ = true;
      awakables_.AwakeForStateChange(GetHandleSignalsStateImplNoLock());
      return MOJO_RESULT_FAILED_PRECONDITION;
    }

    NOTREACHED();
    return MOJO_RESULT_UNKNOWN;
  }

  return MOJO_RESULT_OK;
}

MojoResult MessagePipeDispatcher::ReadMessageImplNoLock(
    void* bytes,
    uint32_t* num_bytes,
    MojoHandle* handles,
    uint32_t* num_handles,
    MojoReadMessageFlags flags) {
  lock().AssertAcquired();

  bool no_space = false;

  // Ensure the provided buffers are large enough to hold the next message.
  // GetMessageIf provides an atomic way to test the next message without
  // committing to removing it from the port's underlying message queue until
  // we are sure we can consume it.

  ports::ScopedMessage message;
  int rv = node_->GetMessageIf(
      port_name_,
      [num_bytes, num_handles, &no_space](const ports::Message& next_message) {
        size_t bytes_to_read = 0;
        if (num_bytes) {
          bytes_to_read = std::min(static_cast<size_t>(*num_bytes),
                                   next_message.num_bytes);
          *num_bytes = next_message.num_bytes;
        }

        size_t handles_to_read = 0;
        if (num_handles) {
          handles_to_read = std::min(static_cast<size_t>(*num_handles),
                                     next_message.num_ports);
          *num_handles = next_message.num_ports;
        }

        if (bytes_to_read < next_message.num_bytes ||
            handles_to_read < next_message.num_ports) {
          no_space = true;
          return false;
        }

        return true;
      },
      &message);

  if (rv != ports::OK) {
    if (rv == ports::ERROR_PORT_UNKNOWN ||
        rv == ports::ERROR_PORT_STATE_UNEXPECTED)
      return MOJO_RESULT_INVALID_ARGUMENT;

    if (rv == ports::ERROR_PORT_PEER_CLOSED) {
      peer_closed_ = true;
      awakables_.AwakeForStateChange(GetHandleSignalsStateImplNoLock());
      return MOJO_RESULT_FAILED_PRECONDITION;
    }

    NOTREACHED();
    return MOJO_RESULT_UNKNOWN;  // TODO: Add a better error code here?
  }

  if (no_space)
    return MOJO_RESULT_RESOURCE_EXHAUSTED;

  if (!message)
    return MOJO_RESULT_SHOULD_WAIT;

  if (!node_->core()->AddDispatchersForReceivedPorts(*message, handles)) {
    // TODO: Close all of the received ports.
    return MOJO_RESULT_UNKNOWN;  // TODO: Add a better error code here?
  }

  memcpy(bytes, message->bytes, message->num_bytes);

  if (!HasMessagesQueuedNoLock()) {
    port_readable_ = false;
    awakables_.AwakeForStateChange(GetHandleSignalsStateImplNoLock());
  }

  return MOJO_RESULT_OK;
}

HandleSignalsState
MessagePipeDispatcher::GetHandleSignalsStateImplNoLock() const {
  lock().AssertAcquired();

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

MojoResult MessagePipeDispatcher::AddAwakableImplNoLock(
    Awakable* awakable,
    MojoHandleSignals signals,
    uintptr_t context,
    HandleSignalsState* signals_state) {
  lock().AssertAcquired();
  HandleSignalsState state = GetHandleSignalsStateImplNoLock();
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

void MessagePipeDispatcher::RemoveAwakableImplNoLock(
    Awakable* awakable,
    HandleSignalsState* signals_state) {
  lock().AssertAcquired();
  awakables_.Remove(awakable);
}

bool MessagePipeDispatcher::BeginTransitImplNoLock() {
  return true;
}

void MessagePipeDispatcher::EndTransitImplNoLock(bool canceled) {
  if (!canceled) {
    // port_name_ has been closed by virtue of having been transferred.
    // This dispatcher needs to be closed as well.
    port_transferred_ = true;
    CloseNoLock();

    // TODO: Need to implement CancelAllAwakablesNoLock.
  }
}

bool MessagePipeDispatcher::HasMessagesQueuedNoLock() {
  // Peek at the queue. If our selector function runs at all, it's not empty.
  //
  // TODO: maybe Node should have an interface for this test?
  bool empty = true;
  ports::ScopedMessage message;
  int rv = node_->GetMessageIf(
      port_name_,
      [&empty](const ports::Message&) {
        empty = false;
        return false;
      },
      &message);

  DCHECK(rv == ports::OK || ports::ERROR_PORT_PEER_CLOSED);

  if (rv == ports::ERROR_PORT_PEER_CLOSED && !peer_closed_) {
    peer_closed_ = true;
    awakables_.AwakeForStateChange(GetHandleSignalsStateImplNoLock());
  }

  return !empty;
}

void MessagePipeDispatcher::OnMessagesAvailable() {
  base::AutoLock dispatcher_lock(lock());
  if (!port_readable_ && HasMessagesQueuedNoLock()) {
    port_readable_ = true;
    awakables_.AwakeForStateChange(GetHandleSignalsStateImplNoLock());
  }
}

}  // namespace edk
}  // namespace mojo
