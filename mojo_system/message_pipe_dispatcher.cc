// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ports/mojo_system/message_pipe_dispatcher.h"


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

MessagePipeDispatcher::~MessagePipeDispatcher() {}

void MessagePipeDispatcher::CloseImplNoLock() {
  lock().AssertAcquired();
  DCHECK(is_closed());
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

  // TODO: Check SendMessage return value?
  node_->SendMessage(port_name_, std::move(message));

  return MOJO_RESULT_OK;
}

MojoResult MessagePipeDispatcher::ReadMessageImplNoLock(
    void* bytes,
    uint32_t* num_bytes,
    MojoHandle* handles,
    uint32_t* num_handles,
    MojoReadMessageFlags flags) {
  lock().AssertAcquired();
  if (incoming_messages_.empty())
    return MOJO_RESULT_SHOULD_WAIT;
  ports::ScopedMessage message = std::move(incoming_messages_.front());
  size_t bytes_to_read = 0;
  if (num_bytes) {
    bytes_to_read = std::min(static_cast<size_t>(*num_bytes),
                             message->num_bytes);
    *num_bytes = message->num_bytes;
  }

  size_t handles_to_read = 0;
  if (num_handles) {
    handles_to_read = std::min(static_cast<size_t>(*num_handles),
                               message->num_ports);
    *num_handles = message->num_ports;
  }

  if (bytes_to_read < message->num_bytes ||
      handles_to_read < message->num_ports) {
    incoming_messages_.front() = std::move(message);
    return MOJO_RESULT_RESOURCE_EXHAUSTED;
  }

  incoming_messages_.pop();

  memcpy(bytes, message->bytes, message->num_bytes);

  // TODO: port transfer
  CHECK_EQ(message->num_ports, 0u);

  return MOJO_RESULT_OK;
}

HandleSignalsState
MessagePipeDispatcher::GetHandleSignalsStateImplNoLock() const {
  lock().AssertAcquired();

  HandleSignalsState rv;
  if (!incoming_messages_.empty()) {
    rv.satisfied_signals |= MOJO_HANDLE_SIGNAL_READABLE;
    rv.satisfiable_signals |= MOJO_HANDLE_SIGNAL_READABLE;
  }
  if (!port_closed_) {
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

void MessagePipeDispatcher::OnMessageAvailable(const ports::PortName& port,
                                               ports::ScopedMessage message) {
  base::AutoLock dispatcher_lock(lock());
  DCHECK(port == port_name_);
  bool should_wake = incoming_messages_.empty();
  incoming_messages_.emplace(std::move(message));
  if (should_wake)
    awakables_.AwakeForStateChange(GetHandleSignalsStateImplNoLock());
}

void MessagePipeDispatcher::OnClosed(const ports::PortName& port) {
  base::AutoLock dispatcher_lock(lock());
  DCHECK(port == port_name_);
  port_closed_ = true;
  awakables_.AwakeForStateChange(GetHandleSignalsStateImplNoLock());
}

}  // namespace edk
}  // namespace mojo
