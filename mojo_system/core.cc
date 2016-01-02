// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ports/mojo_system/core.h"

#include <utility>

#include "base/bind.h"
#include "base/containers/stack_container.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/time/time.h"
#include "crypto/random.h"
#include "mojo/edk/embedder/embedder_internal.h"
#include "mojo/edk/system/configuration.h"
#include "mojo/edk/system/handle_signals_state.h"
#include "mojo/edk/system/waiter.h"
#include "ports/include/ports.h"
#include "ports/mojo_system/channel.h"
#include "ports/mojo_system/message_pipe_dispatcher.h"
#include "ports/mojo_system/shared_buffer_dispatcher.h"
#include "ports/mojo_system/wait_set_dispatcher.h"

namespace mojo {
namespace edk {

namespace {

void OnRemotePeerConnected(
    Core* core,
    const ports::PortName& local_port_name,
    const base::Callback<void(ScopedMessagePipeHandle)>& callback) {
  DLOG(INFO) << "Remote peer connected for " << local_port_name;
  callback.Run(ScopedMessagePipeHandle(MessagePipeHandle(core->AddDispatcher(
      new MessagePipeDispatcher(core->node(), local_port_name)))));
}

}  // namespace

Core::Core() : node_(this) {}

Core::~Core() {}

void Core::SetIOTaskRunner(scoped_refptr<base::TaskRunner> io_task_runner) {
  io_task_runner_ = io_task_runner;
}

void Core::AddChild(ScopedPlatformHandle platform_handle) {
  node_.ConnectToChild(std::move(platform_handle));
}

void Core::InitChild(ScopedPlatformHandle platform_handle) {
  node_.ConnectToParent(std::move(platform_handle));
}

MojoHandle Core::AddDispatcher(scoped_refptr<Dispatcher> dispatcher) {
  base::AutoLock lock(handles_lock_);
  return handles_.AddDispatcher(dispatcher);
}

bool Core::AddDispatchersForReceivedPorts(const ports::Message& message,
                                          MojoHandle* handles) {
  std::vector<Dispatcher::DispatcherInTransit> dispatchers(message.num_ports());
  for (size_t i = 0; i < message.num_ports(); ++i) {
    Dispatcher::DispatcherInTransit& d = dispatchers[i];
    d.dispatcher = new MessagePipeDispatcher(&node_, message.ports()[i]);
  }
  return AddDispatchersFromTransit(dispatchers, handles);
}

bool Core::AddDispatchersFromTransit(
    const std::vector<Dispatcher::DispatcherInTransit>& dispatchers,
    MojoHandle* handles) {
  bool failed = false;
  {
    base::AutoLock lock(handles_lock_);
    if (!handles_.AddDispatchersFromTransit(dispatchers, handles))
      failed = true;
  }
  if (failed) {
    for (auto d : dispatchers)
      d.dispatcher->Close();
    return false;
  }
  return true;
}

void Core::CreateParentMessagePipe(
    const std::string& token,
    const base::Callback<void(ScopedMessagePipeHandle)>& callback) {
  ports::PortName port_name;
  node_.CreateUninitializedPort(&port_name);
  node_.ReservePortForToken(
      port_name, token,
      base::Bind(&OnRemotePeerConnected, base::Unretained(this), port_name,
                 callback));
}

void Core::CreateChildMessagePipe(
    const std::string& token,
    const base::Callback<void(ScopedMessagePipeHandle)>& callback) {
  ports::PortName port_name;
  node_.CreateUninitializedPort(&port_name);
  node_.ConnectToParentPortByToken(
      token, port_name,
      base::Bind(&OnRemotePeerConnected, base::Unretained(this), port_name,
                 callback));
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
  scoped_refptr<Dispatcher> dispatcher;
  {
    base::AutoLock lock(handles_lock_);
    MojoResult rv = handles_.GetAndRemoveDispatcher(handle, &dispatcher);
    if (rv != MOJO_RESULT_OK)
      return rv;
  }
  dispatcher->Close();
  return MOJO_RESULT_OK;
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
  if (!wait_set_handle)
    return MOJO_RESULT_INVALID_ARGUMENT;

  scoped_refptr<WaitSetDispatcher> dispatcher = new WaitSetDispatcher();
  MojoHandle h = AddDispatcher(dispatcher);
  if (h == MOJO_HANDLE_INVALID) {
    LOG(ERROR) << "Handle table full";
    dispatcher->Close();
    return MOJO_RESULT_RESOURCE_EXHAUSTED;
  }

  *wait_set_handle = h;
  return MOJO_RESULT_OK;
}

MojoResult Core::AddHandle(MojoHandle wait_set_handle,
                           MojoHandle handle,
                           MojoHandleSignals signals) {
  scoped_refptr<Dispatcher> wait_set_dispatcher(GetDispatcher(wait_set_handle));
  if (!wait_set_dispatcher)
    return MOJO_RESULT_INVALID_ARGUMENT;

  scoped_refptr<Dispatcher> dispatcher(GetDispatcher(handle));
  if (!dispatcher)
    return MOJO_RESULT_INVALID_ARGUMENT;

  return wait_set_dispatcher->AddWaitingDispatcher(dispatcher, signals, handle);
}

MojoResult Core::RemoveHandle(MojoHandle wait_set_handle,
                        MojoHandle handle) {
  scoped_refptr<Dispatcher> wait_set_dispatcher(GetDispatcher(wait_set_handle));
  if (!wait_set_dispatcher)
    return MOJO_RESULT_INVALID_ARGUMENT;

  scoped_refptr<Dispatcher> dispatcher(GetDispatcher(handle));
  if (!dispatcher)
    return MOJO_RESULT_INVALID_ARGUMENT;

  return wait_set_dispatcher->RemoveWaitingDispatcher(dispatcher);
}

MojoResult Core::GetReadyHandles(MojoHandle wait_set_handle,
                                 uint32_t* count,
                                 MojoHandle* handles,
                                 MojoResult* results,
                                 MojoHandleSignalsState* signals_states) {
  if (!handles || !count || !(*count) || !results)
    return MOJO_RESULT_INVALID_ARGUMENT;

  scoped_refptr<Dispatcher> wait_set_dispatcher(GetDispatcher(wait_set_handle));
  if (!wait_set_dispatcher)
    return MOJO_RESULT_INVALID_ARGUMENT;

  DispatcherVector awoken_dispatchers;
  base::StackVector<uintptr_t, 16> contexts;
  contexts->assign(*count, MOJO_HANDLE_INVALID);

  MojoResult result = wait_set_dispatcher->GetReadyDispatchers(
      count, &awoken_dispatchers, results, contexts->data());

  if (result == MOJO_RESULT_OK) {
    for (size_t i = 0; i < *count; i++) {
      handles[i] = static_cast<MojoHandle>(contexts[i]);
      if (signals_states)
        signals_states[i] = awoken_dispatchers[i]->GetHandleSignalsState();
    }
  }

  return result;
}

MojoResult Core::CreateMessagePipe(
    const MojoCreateMessagePipeOptions* options,
    MojoHandle* message_pipe_handle0,
    MojoHandle* message_pipe_handle1) {
  ports::PortName port0, port1;
  node_.CreatePortPair(&port0, &port1);
  *message_pipe_handle0 = AddDispatcher(
      new MessagePipeDispatcher(&node_, port0));
  *message_pipe_handle1 = AddDispatcher(
      new MessagePipeDispatcher(&node_, port1));
  return MOJO_RESULT_OK;
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

  if (num_handles == 0)  // Fast path: no handles.
    return dispatcher->WriteMessage(bytes, num_bytes, nullptr, 0, flags);

  std::vector<Dispatcher::DispatcherInTransit> dispatchers;
  {
    base::AutoLock lock(handles_lock_);
    MojoResult rv = handles_.BeginTransit(handles, num_handles, &dispatchers);
    if (rv != MOJO_RESULT_OK)
      return rv;
  }
  DCHECK_EQ(num_handles, dispatchers.size());

  MojoResult rv = dispatcher->WriteMessage(
      bytes, num_bytes, dispatchers.data(), num_handles, flags);

  {
    base::AutoLock lock(handles_lock_);
    if (rv == MOJO_RESULT_OK) {
      handles_.CompleteTransit(dispatchers);
    } else {
      handles_.CancelTransit(dispatchers);
    }
  }

  return rv;
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
  return dispatcher->ReadMessage(bytes, num_bytes, handles, num_handles, flags);
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
  MojoCreateSharedBufferOptions validated_options = {};
  MojoResult result = SharedBufferDispatcher::ValidateCreateOptions(
      options, &validated_options);
  if (result != MOJO_RESULT_OK)
    return result;

  scoped_refptr<SharedBufferDispatcher> dispatcher;
  result = SharedBufferDispatcher::Create(
      internal::g_platform_support, validated_options, num_bytes, &dispatcher);
  if (result != MOJO_RESULT_OK) {
    DCHECK(!dispatcher);
    return result;
  }

  *shared_buffer_handle = AddDispatcher(dispatcher);
  if (*shared_buffer_handle == MOJO_HANDLE_INVALID) {
    LOG(ERROR) << "Handle table full";
    dispatcher->Close();
    return MOJO_RESULT_RESOURCE_EXHAUSTED;
  }

  return MOJO_RESULT_OK;
}

MojoResult Core::DuplicateBufferHandle(
    MojoHandle buffer_handle,
    const MojoDuplicateBufferHandleOptions* options,
    MojoHandle* new_buffer_handle) {
  scoped_refptr<Dispatcher> dispatcher(GetDispatcher(buffer_handle));
  if (!dispatcher)
    return MOJO_RESULT_INVALID_ARGUMENT;

  // Don't verify |options| here; that's the dispatcher's job.
  scoped_refptr<Dispatcher> new_dispatcher;
  MojoResult result =
      dispatcher->DuplicateBufferHandle(options, &new_dispatcher);
  if (result != MOJO_RESULT_OK)
    return result;

  *new_buffer_handle = AddDispatcher(new_dispatcher);
  if (*new_buffer_handle == MOJO_HANDLE_INVALID) {
    LOG(ERROR) << "Handle table full";
    dispatcher->Close();
    return MOJO_RESULT_RESOURCE_EXHAUSTED;
  }

  return MOJO_RESULT_OK;
}

MojoResult Core::MapBuffer(MojoHandle buffer_handle,
                           uint64_t offset,
                           uint64_t num_bytes,
                           void** buffer,
                           MojoMapBufferFlags flags) {
  scoped_refptr<Dispatcher> dispatcher(GetDispatcher(buffer_handle));
  if (!dispatcher)
    return MOJO_RESULT_INVALID_ARGUMENT;

  scoped_ptr<PlatformSharedBufferMapping> mapping;
  MojoResult result = dispatcher->MapBuffer(offset, num_bytes, flags, &mapping);
  if (result != MOJO_RESULT_OK)
    return result;

  DCHECK(mapping);
  void* address = mapping->GetBase();
  {
    base::AutoLock locker(mapping_table_lock_);
    result = mapping_table_.AddMapping(std::move(mapping));
  }
  if (result != MOJO_RESULT_OK)
    return result;

  *buffer = address;
  return MOJO_RESULT_OK;
}

MojoResult Core::UnmapBuffer(void* buffer) {
  base::AutoLock lock(mapping_table_lock_);
  return mapping_table_.RemoveMapping(buffer);
}

scoped_refptr<Dispatcher> Core::GetDispatcher(MojoHandle handle) {
  base::AutoLock lock(handles_lock_);
  return handles_.GetDispatcher(handle);
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
