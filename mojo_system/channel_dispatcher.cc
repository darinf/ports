// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ports/mojo_system/channel_dispatcher.h"

#include <cstring>

#include "ports/mojo_system/channel.h"

namespace mojo {
namespace edk {

ChannelDispatcher::Message::Message(const void* data,
                                    size_t num_bytes,
                                    ScopedPlatformHandleVectorPtr handles)
    : data_(num_bytes), handles_(std::move(handles)) {
  memcpy(data_.data(), data, num_bytes);
}

ChannelDispatcher::Message::~Message() {}

ChannelDispatcher::ChannelDispatcher(
    ScopedPlatformHandle platform_handle,
    scoped_refptr<base::TaskRunner> io_task_runner)
    : channel_(Channel::Create(this, std::move(platform_handle),
                               io_task_runner)) {}

Dispatcher::Type ChannelDispatcher::GetType() const {
  return Type::MESSAGE_PIPE;
}

MojoResult ChannelDispatcher::WriteMessageImplNoLock(
    ports::ScopedMessage message,
    MojoWriteMessageFlags flags) {

  return MOJO_RESULT_OK;
}

MojoResult ChannelDispatcher::ReadMessageImplNoLock(
    MojoReadMessageFlags flags,
    ports::ScopedMessage* message) {
  return MOJO_RESULT_OK;
}

HandleSignalsState
ChannelDispatcher::GetHandleSignalsStateImplNoLock() const {
  lock().AssertAcquired();

  HandleSignalsState rv;
  if (!incoming_messages_.empty())
    rv.satisfied_signals |= MOJO_HANDLE_SIGNAL_READABLE;
  if (!is_closed()) {
    rv.satisfiable_signals |= MOJO_HANDLE_SIGNAL_READABLE;
    rv.satisfied_signals |= MOJO_HANDLE_SIGNAL_WRITABLE;
    rv.satisfiable_signals |= MOJO_HANDLE_SIGNAL_WRITABLE;
  } else {
    rv.satisfied_signals |= MOJO_HANDLE_SIGNAL_PEER_CLOSED;
  }
  rv.satisfiable_signals |= MOJO_HANDLE_SIGNAL_PEER_CLOSED;
  return rv;
}

MojoResult ChannelDispatcher::AddAwakableImplNoLock(
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

void ChannelDispatcher::RemoveAwakableImplNoLock(
    Awakable* awakable,
    HandleSignalsState* signals_state) {
}

void ChannelDispatcher::OnChannelRead(Channel::IncomingMessage* message) {
  base::AutoLock dispatcher_lock(lock());
  incoming_messages_.emplace(new Message(
      message->data(), message->num_bytes(), message->TakeHandles()));
}

void ChannelDispatcher::OnChannelError() {
  {
    base::AutoLock dispatcher_lock(lock());
    if (is_closed())
      return;
  }
  Close();
  {
    base::AutoLock dispatcher_lock(lock());
    awakables_.AwakeForStateChange(GetHandleSignalsStateImplNoLock());
  }
}

ChannelDispatcher::~ChannelDispatcher() {}

}  // namespace edk
}  // namespace mojo
