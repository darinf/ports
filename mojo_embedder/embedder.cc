// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/edk/embedder/embedder.h"

#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "ports/include/ports.h"
#include "ports/mojo_system/core.h"
#include "ports/mojo_system/channel_dispatcher.h"

namespace mojo {
namespace edk {

class Core;
class PlatformSupport;

namespace internal {

Core* g_core;
scoped_refptr<base::TaskRunner> g_io_thread_task_runner;

// Only here to satisfy some messy link dependency in old EDK test code
PlatformSupport* g_platform_support;

}  // namespace internal

void SetMaxMessageSize(size_t bytes) {
}

void PreInitializeParentProcess() {
}

void PreInitializeChildProcess() {
}

ScopedPlatformHandle ChildProcessLaunched(base::ProcessHandle child_process) {
  NOTIMPLEMENTED();
  return ScopedPlatformHandle();
}

void ChildProcessLaunched(base::ProcessHandle child_process,
                          ScopedPlatformHandle server_pipe) {
  CHECK(internal::g_core);
  internal::g_core->AddChild(std::move(server_pipe));
}

void SetParentPipeHandle(ScopedPlatformHandle pipe) {
  CHECK(internal::g_core);
  internal::g_core->InitChild(std::move(pipe));
}

void Init() {
  internal::g_core = new Core();
}

MojoResult AsyncWait(MojoHandle handle,
                     MojoHandleSignals signals,
                     const base::Callback<void(MojoResult)>& callback) {
  NOTIMPLEMENTED();
  return MOJO_RESULT_UNIMPLEMENTED;
}

MojoResult CreatePlatformHandleWrapper(
    ScopedPlatformHandle platform_handle,
    MojoHandle* platform_handle_wrapper_handle) {
  NOTIMPLEMENTED();
  return MOJO_RESULT_UNIMPLEMENTED;
}

MojoResult PassWrappedPlatformHandle(MojoHandle platform_handle_wrapper_handle,
                                     ScopedPlatformHandle* platform_handle) {
  NOTIMPLEMENTED();
  return MOJO_RESULT_UNIMPLEMENTED;
}

void InitIPCSupport(ProcessDelegate* process_delegate,
                    scoped_refptr<base::TaskRunner> io_thread_task_runner) {
  CHECK(internal::g_core);
  CHECK(!internal::g_io_thread_task_runner);
  internal::g_io_thread_task_runner = io_thread_task_runner;
  internal::g_core->SetIOTaskRunner(io_thread_task_runner);
}

void ShutdownIPCSupportOnIOThread() {
}

void ShutdownIPCSupport() {
  NOTIMPLEMENTED();
}

ScopedMessagePipeHandle CreateMessagePipe(
    ScopedPlatformHandle platform_handle) {
  scoped_refptr<ChannelDispatcher> dispatcher =
      new ChannelDispatcher(std::move(platform_handle),
                            internal::g_io_thread_task_runner);
  ScopedMessagePipeHandle rv(
      MessagePipeHandle(internal::g_core->AddDispatcher(dispatcher)));
  CHECK(rv.is_valid());
  return rv;
}

}  // namespace edk
}  // namespace mojo
