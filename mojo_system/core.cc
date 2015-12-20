// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ports/mojo_system/core.h"

#include <utility>

#include "base/logging.h"
#include "base/macros.h"
#include "base/time/time.h"
#include "crypto/random.h"
#include "mojo/edk/system/configuration.h"
#include "mojo/edk/system/handle_signals_state.h"
#include "mojo/edk/system/waiter.h"
#include "ports/include/ports.h"
#include "ports/mojo_system/channel.h"
#include "ports/mojo_system/child_node.h"
#include "ports/mojo_system/parent_node.h"

namespace mojo {
namespace edk {

Core::Core() {}

Core::~Core() {}

void Core::SetIOTaskRunner(scoped_refptr<base::TaskRunner> io_task_runner) {
  io_task_runner_ = io_task_runner;
}

void Core::AddChild(ScopedPlatformHandle platform_handle) {
  scoped_ptr<NodeChannel> channel(
      new NodeChannel(std::move(platform_handle), io_task_runner_));
  if (!node_)
    node_.reset(new ParentNode);
  ParentNode* parent_node = static_cast<ParentNode*>(node_.get());
  parent_node->AddChild(std::move(channel));
}

void Core::InitChild(ScopedPlatformHandle platform_handle) {
  CHECK(!node_);
  scoped_ptr<NodeChannel> parent_channel(
      new NodeChannel(std::move(platform_handle), io_task_runner_));
  node_.reset(new ChildNode(std::move(parent_channel)));
}

MojoHandle Core::AddDispatcher(scoped_refptr<Dispatcher> dispatcher) {
  MojoHandle handle = next_handle_++;
  dispatchers_.insert(std::make_pair(handle, dispatcher));
  return handle;
}

MojoResult Core::AsyncWait(MojoHandle handle,
                           MojoHandleSignals signals,
                           const base::Callback<void(MojoResult)>& callback) {
  NOTIMPLEMENTED();
  return 0;
}

MojoTimeTicks Core::GetTimeTicksNow() {
  return base::TimeTicks::Now().ToInternalValue();
}

MojoResult Core::Close(MojoHandle handle) {
  auto dispatcher = GetDispatcher(handle);
  if (!dispatcher)
    return MOJO_RESULT_INVALID_ARGUMENT;
  return dispatcher->Close();
}

MojoResult Core::Wait(MojoHandle handle,
                      MojoHandleSignals signals,
                      MojoDeadline deadline,
                      MojoHandleSignalsState* signals_state) {
  uint32_t unused = static_cast<uint32_t>(-1);
  HandleSignalsState hss;
  MojoResult rv = WaitManyInternal(&handle, &signals, 1, deadline, &unused,
                                   signals_state ? &hss : nullptr);
  if (rv != MOJO_RESULT_INVALID_ARGUMENT && signals_state)
    *signals_state = hss;
  return rv;
}

MojoResult Core::WaitMany(const MojoHandle* handles,
                          const MojoHandleSignals* signals,
                          uint32_t num_handles,
                          MojoDeadline deadline,
                          uint32_t* result_index,
                          MojoHandleSignalsState* signals_state) {
  if (num_handles < 1)
    return MOJO_RESULT_INVALID_ARGUMENT;
  if (num_handles > GetConfiguration().max_wait_many_num_handles)
    return MOJO_RESULT_RESOURCE_EXHAUSTED;

  uint32_t index = static_cast<uint32_t>(-1);
  MojoResult rv;
  if (!signals_state) {
    rv = WaitManyInternal(handles, signals, num_handles, deadline, &index,
                          nullptr);
  } else {
    // Note: The |reinterpret_cast| is safe, since |HandleSignalsState| is a
    // subclass of |MojoHandleSignalsState| that doesn't add any data members.
    rv = WaitManyInternal(handles, signals, num_handles, deadline, &index,
                          reinterpret_cast<HandleSignalsState*>(signals_state));
  }
  if (index != static_cast<uint32_t>(-1) && result_index)
    *result_index = index;
  return rv;
}

MojoResult Core::CreateWaitSet(MojoHandle* wait_set_handle) {
  NOTIMPLEMENTED();
  return MOJO_RESULT_UNIMPLEMENTED;
}

MojoResult Core::AddHandle(MojoHandle wait_set_handle,
                           MojoHandle handle,
                           MojoHandleSignals signals) {
  NOTIMPLEMENTED();
  return MOJO_RESULT_UNIMPLEMENTED;
}

MojoResult Core::RemoveHandle(MojoHandle wait_set_handle,
                        MojoHandle handle) {
  NOTIMPLEMENTED();
  return MOJO_RESULT_UNIMPLEMENTED;
}

MojoResult Core::GetReadyHandles(MojoHandle wait_set_handle,
                                 uint32_t* count,
                                 MojoHandle* handles,
                                 MojoResult* results,
                                 MojoHandleSignalsState* signals_states) {
  NOTIMPLEMENTED();
  return MOJO_RESULT_UNIMPLEMENTED;
}

MojoResult Core::CreateMessagePipe(
    const MojoCreateMessagePipeOptions* options,
    MojoHandle* message_pipe_handle0,
    MojoHandle* message_pipe_handle1) {
  NOTIMPLEMENTED();
  return MOJO_RESULT_UNIMPLEMENTED;
}

MojoResult Core::WriteMessage(MojoHandle message_pipe_handle,
                              const void* bytes,
                              uint32_t num_bytes,
                              const MojoHandle* handles,
                              uint32_t num_handles,
                              MojoWriteMessageFlags flags) {
  auto dispatcher = GetDispatcher(message_pipe_handle);
  if (!dispatcher || dispatcher->GetType() != Dispatcher::Type::MESSAGE_PIPE)
    return MOJO_RESULT_INVALID_ARGUMENT;
  return dispatcher->WriteMessage(ports::ScopedMessage(), flags);
}

MojoResult Core::ReadMessage(MojoHandle message_pipe_handle,
                             void* bytes,
                             uint32_t* num_bytes,
                             MojoHandle* handles,
                             uint32_t* num_handles,
                             MojoReadMessageFlags flags) {
  auto dispatcher = GetDispatcher(message_pipe_handle);
  if (!dispatcher || dispatcher->GetType() != Dispatcher::Type::MESSAGE_PIPE)
    return MOJO_RESULT_INVALID_ARGUMENT;
  ports::ScopedMessage message;
  return dispatcher->ReadMessage(flags, &message);
}

MojoResult Core::CreateDataPipe(
    const MojoCreateDataPipeOptions* options,
    MojoHandle* data_pipe_producer_handle,
    MojoHandle* data_pipe_consumer_handle) {
  NOTIMPLEMENTED();
  return MOJO_RESULT_UNIMPLEMENTED;
}

MojoResult Core::WriteData(MojoHandle data_pipe_producer_handle,
                           const void* elements,
                           uint32_t* num_bytes,
                           MojoWriteDataFlags flags) {
  NOTIMPLEMENTED();
  return MOJO_RESULT_UNIMPLEMENTED;
}

MojoResult Core::BeginWriteData(MojoHandle data_pipe_producer_handle,
                                void** buffer,
                                uint32_t* buffer_num_bytes,
                                MojoWriteDataFlags flags) {
  NOTIMPLEMENTED();
  return MOJO_RESULT_UNIMPLEMENTED;
}

MojoResult Core::EndWriteData(MojoHandle data_pipe_producer_handle,
                              uint32_t num_bytes_written) {
  NOTIMPLEMENTED();
  return MOJO_RESULT_UNIMPLEMENTED;
}

MojoResult Core::ReadData(MojoHandle data_pipe_consumer_handle,
                          void* elements,
                          uint32_t* num_bytes,
                          MojoReadDataFlags flags) {
  NOTIMPLEMENTED();
  return MOJO_RESULT_UNIMPLEMENTED;
}

MojoResult Core::BeginReadData(MojoHandle data_pipe_consumer_handle,
                               const void** buffer,
                               uint32_t* buffer_num_bytes,
                               MojoReadDataFlags flags) {
  NOTIMPLEMENTED();
  return MOJO_RESULT_UNIMPLEMENTED;
}

MojoResult Core::EndReadData(MojoHandle data_pipe_consumer_handle,
                             uint32_t num_bytes_read) {
  NOTIMPLEMENTED();
  return MOJO_RESULT_UNIMPLEMENTED;
}

MojoResult Core::CreateSharedBuffer(
    const MojoCreateSharedBufferOptions* options,
    uint64_t num_bytes,
    MojoHandle* shared_buffer_handle) {
  NOTIMPLEMENTED();
  return MOJO_RESULT_UNIMPLEMENTED;
}

MojoResult Core::DuplicateBufferHandle(
    MojoHandle buffer_handle,
    const MojoDuplicateBufferHandleOptions* options,
    MojoHandle* new_buffer_handle) {
  NOTIMPLEMENTED();
  return MOJO_RESULT_UNIMPLEMENTED;
}

MojoResult Core::MapBuffer(MojoHandle buffer_handle,
                           uint64_t offset,
                           uint64_t num_bytes,
                           void** buffer,
                           MojoMapBufferFlags flags) {
  NOTIMPLEMENTED();
  return MOJO_RESULT_UNIMPLEMENTED;
}

MojoResult Core::UnmapBuffer(void* buffer) {
  NOTIMPLEMENTED();
  return MOJO_RESULT_UNIMPLEMENTED;
}

scoped_refptr<Dispatcher> Core::GetDispatcher(MojoHandle handle) {
  auto iter = dispatchers_.find(handle);
  if (iter == dispatchers_.end())
    return nullptr;
  return iter->second;
}

MojoResult Core::WaitManyInternal(const MojoHandle* handles,
                                  const MojoHandleSignals* signals,
                                  uint32_t num_handles,
                                  MojoDeadline deadline,
                                  uint32_t *result_index,
                                  HandleSignalsState* signals_states) {
  // TODO: Review this... Ripped from existing EDK.
  CHECK(handles);
  CHECK(signals);
  DCHECK_GT(num_handles, 0u);
  if (result_index) {
    DCHECK_EQ(*result_index, static_cast<uint32_t>(-1));
  }

  DispatcherVector dispatchers;
  dispatchers.reserve(num_handles);
  for (uint32_t i = 0; i < num_handles; i++) {
    scoped_refptr<Dispatcher> dispatcher = GetDispatcher(handles[i]);
    if (!dispatcher) {
      if (result_index)
        *result_index = i;
      return MOJO_RESULT_INVALID_ARGUMENT;
    }
    dispatchers.push_back(dispatcher);
  }

  // TODO(vtl): Should make the waiter live (permanently) in TLS.
  Waiter waiter;
  waiter.Init();

  uint32_t i;
  MojoResult rv = MOJO_RESULT_OK;
  for (i = 0; i < num_handles; i++) {
    rv = dispatchers[i]->AddAwakable(
        &waiter, signals[i], i, signals_states ? &signals_states[i] : nullptr);
    if (rv != MOJO_RESULT_OK) {
      if (result_index)
        *result_index = i;
      break;
    }
  }
  uint32_t num_added = i;

  if (rv == MOJO_RESULT_ALREADY_EXISTS) {
    rv = MOJO_RESULT_OK;  // The i-th one is already "triggered".
  } else if (rv == MOJO_RESULT_OK) {
    uintptr_t uintptr_result = *result_index;
    rv = waiter.Wait(deadline, &uintptr_result);
    *result_index = static_cast<uint32_t>(uintptr_result);
  }

  // Make sure no other dispatchers try to wake |waiter| for the current
  // |Wait()|/|WaitMany()| call. (Only after doing this can |waiter| be
  // destroyed, but this would still be required if the waiter were in TLS.)
  for (i = 0; i < num_added; i++) {
    dispatchers[i]->RemoveAwakable(
        &waiter, signals_states ? &signals_states[i] : nullptr);
  }
  if (signals_states) {
    for (; i < num_handles; i++)
      signals_states[i] = dispatchers[i]->GetHandleSignalsState();
  }

  return rv;
}

}  // namespace edk
}  // namespace mojo
