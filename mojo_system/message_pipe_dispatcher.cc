// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ports/mojo_system/message_pipe_dispatcher.h"

#include "ports/mojo_system/core.h"

namespace mojo {
namespace edk {

MessagePipeDispatcher::MessagePipeDispatcher(Node* node,
                                             const ports::PortName& port_name)
    : node_(node), port_name_(port_name) {
  node_->SetPortObserver(port_name_, this);
}

Dispatcher::Type MessagePipeDispatcher::GetType() const {
  return Type::MESSAGE_PIPE;
}

MessagePipeDispatcher::~MessagePipeDispatcher() {
  node_->SetPortObserver(port_name_, nullptr);
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

  // TODO: More detailed result code on failure
  if (rv != ports::OK)
    return MOJO_RESULT_INVALID_ARGUMENT;

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
    if (rv == ports::ERROR_PORT_UNKNOWN)
      return MOJO_RESULT_INVALID_ARGUMENT;
    if (rv == ports::ERROR_PORT_PEER_CLOSED) {
      peer_closed_ = true;
      awakables_.AwakeForStateChange(GetHandleSignalsStateImplNoLock());
      return MOJO_RESULT_FAILED_PRECONDITION;
    }
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

  // Check to see if the queue is completely drained so we can unset the
  // READABLE signal on the handle.
  //
  // TODO: maybe there's a better way to do this?
  bool still_readable = false;
  rv = node_->GetMessageIf(
      port_name_, [&still_readable](const ports::Message&) {
        still_readable = true;
        return false;
      }, &message);
  if (!still_readable ) {
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

void MessagePipeDispatcher::OnMessagesAvailable() {
  LOG(ERROR) << "OnMessagesAvailable";

  base::AutoLock dispatcher_lock(lock());
  port_readable_ = true;
  awakables_.AwakeForStateChange(GetHandleSignalsStateImplNoLock());
}

}  // namespace edk
}  // namespace mojo
