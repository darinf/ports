// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ports/mojo_system/message_pipe_dispatcher.h"


namespace mojo {
namespace edk {

MessagePipeDispatcher::MessagePipeDispatcher(Node* node,
                                             const ports::PortName& port_name,
                                             bool connected)
    : connected_(connected), node_(node), port_name_(port_name) {
  node_->SetPortObserver(port_name_, this);
}

Dispatcher::Type MessagePipeDispatcher::GetType() const {
  return Type::MESSAGE_PIPE;
}

void MessagePipeDispatcher::SetRemotePeer(const ports::NodeName& peer_node,
                                          const ports::PortName& peer_port) {
  base::AutoLock dispatcher_lock(lock());
  DCHECK(!connected_);
  int rv = node_->InitializePort(port_name_, peer_node, peer_port);
  DCHECK_EQ(rv, ports::OK);
  connected_ = true;
  awakables_.AwakeForStateChange(GetHandleSignalsStateImplNoLock());
}

MessagePipeDispatcher::~MessagePipeDispatcher() {}

void MessagePipeDispatcher::CloseImplNoLock() {
  lock().AssertAcquired();
  DCHECK(is_closed());
  if (!port_closed_)
    node_->ClosePort(port_name_);
}

MojoResult MessagePipeDispatcher::WriteMessageImplNoLock(
    const void* bytes,
    uint32_t num_bytes,
    const DispatcherInTransit* dispatchers,
    uint32_t num_dispatchers,
    MojoWriteMessageFlags flags) {
  lock().AssertAcquired();
  if (!connected_)
    return MOJO_RESULT_SHOULD_WAIT;
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
    DispatcherInTransit* dispatchers,
    uint32_t* num_dispatchers,
    MojoReadMessageFlags flags) {
  lock().AssertAcquired();
  if (!connected_ || incoming_messages_.empty())
    return MOJO_RESULT_SHOULD_WAIT;
  ports::ScopedMessage message = std::move(incoming_messages_.front());
  size_t bytes_to_read = 0;
  if (num_bytes) {
    bytes_to_read = std::min(static_cast<size_t>(*num_bytes),
                             message->num_bytes);
    *num_bytes = message->num_bytes;
  }

  size_t handles_to_read = 0;
  if (num_dispatchers) {
    handles_to_read = std::min(static_cast<size_t>(*num_dispatchers),
                               message->num_ports);
    *num_dispatchers = message->num_ports;
  }

  if (bytes_to_read < message->num_bytes ||
      handles_to_read < message->num_ports) {
    incoming_messages_.front() = std::move(message);
    return MOJO_RESULT_RESOURCE_EXHAUSTED;
  }

  incoming_messages_.pop();

  memcpy(bytes, message->bytes, message->num_bytes);

  for (size_t i = 0; i < message->num_ports; ++i) {
    DispatcherInTransit& d = dispatchers[i];
    d.dispatcher = new MessagePipeDispatcher(
        node_, message->ports[i].name, true /* connected */);
  }

  // TODO: support reading other types of handles

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
    if (connected_)
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
  DCHECK(!self_while_in_transit_);
  self_while_in_transit_ = this;
  return true;
}

void MessagePipeDispatcher::EndTransitImplNoLock(bool canceled) {
  DCHECK(self_while_in_transit_);

  // We only release here if the transit was canceled. Otherwise we wait until
  // the local port is actually closed. See OnClosed() below.
  if (canceled)
    self_while_in_transit_ = nullptr;
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
  scoped_refptr<MessagePipeDispatcher> self;
  {
    base::AutoLock dispatcher_lock(lock());
    DCHECK(port == port_name_);
    port_closed_ = true;
    awakables_.AwakeForStateChange(GetHandleSignalsStateImplNoLock());

    // Retain a reference to |this| in case we're an orphaned dispatcher waiting
    // for closure as a proxy. Once |self| goes out of scope, if not null,
    // we will be deleted.
    self = self_while_in_transit_;
    self_while_in_transit_ = nullptr;
  }

  self->Close();
}

}  // namespace edk
}  // namespace mojo
