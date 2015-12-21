// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ports/mojo_system/channel_dispatcher.h"

#include <algorithm>
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
    const void* bytes,
    uint32_t num_bytes,
    const MojoHandle* handles,
    uint32_t num_handles,
    MojoWriteMessageFlags flags) {
  std::vector<char> data(num_bytes);
  memcpy(data.data(), bytes, num_bytes);
  // TODO: handles
  Channel::OutgoingMessagePtr message(
      new Channel::OutgoingMessage(std::move(data), nullptr));
  channel_->Write(std::move(message));
  return MOJO_RESULT_OK;
}

MojoResult ChannelDispatcher::ReadMessageImplNoLock(
    void* bytes,
    uint32_t* num_bytes,
    MojoHandle* handles,
    uint32_t* num_handles,
    MojoReadMessageFlags flags) {
  lock().AssertAcquired();
  if (incoming_messages_.empty())
    return MOJO_RESULT_SHOULD_WAIT;
  MessagePtr& message = incoming_messages_.front();

  size_t bytes_to_read = 0;
  if (num_bytes) {
    bytes_to_read = std::min(static_cast<size_t>(*num_bytes),
                             message->num_bytes());
    *num_bytes = message->num_bytes();
  }

  size_t handles_to_read = 0;
  if (num_handles) {
    handles_to_read = std::min(static_cast<size_t>(*num_handles),
                               message->num_handles());
    *num_handles = message->num_handles();
  }

  if (bytes_to_read < message->num_bytes() ||
      handles_to_read < message->num_handles()) {
    return MOJO_RESULT_RESOURCE_EXHAUSTED;
  }

  memcpy(bytes, message->data(), bytes_to_read);
  // TODO: handles

  incoming_messages_.pop();

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
  awakables_.Remove(awakable);
}

void ChannelDispatcher::OnChannelRead(Channel::IncomingMessage* message) {
  base::AutoLock dispatcher_lock(lock());
  incoming_messages_.emplace(new Message(
      message->data(), message->num_bytes(), message->TakeHandles()));
  awakables_.AwakeForStateChange(GetHandleSignalsStateImplNoLock());
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
