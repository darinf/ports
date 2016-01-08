// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/edk/embedder/embedder.h"

#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "mojo/edk/embedder/embedder_internal.h"
#include "mojo/edk/embedder/simple_platform_support.h"
#include "mojo/edk/system/core.h"
#include "mojo/edk/system/ports/ports.h"

namespace mojo {
namespace edk {

class Core;
class PlatformSupport;

namespace internal {

Core* g_core;
base::TaskRunner* g_io_thread_task_runner;

PlatformSupport* g_platform_support;

Core* GetCore() { return g_core; }

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
  internal::g_platform_support = new SimplePlatformSupport();
}

MojoResult AsyncWait(MojoHandle handle,
                     MojoHandleSignals signals,
                     const base::Callback<void(MojoResult)>& callback) {
  return internal::g_core->AsyncWait(handle, signals, callback);
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

  // TODO: Get rid of this global. At worst, it's still accessible from g_core.
  internal::g_io_thread_task_runner = io_thread_task_runner.get();
  internal::g_io_thread_task_runner->AddRef();

  internal::g_core->SetIOTaskRunner(io_thread_task_runner);
}

void ShutdownIPCSupportOnIOThread() {
}

void ShutdownIPCSupport() {
  NOTIMPLEMENTED();
}

ScopedMessagePipeHandle CreateMessagePipe(
    ScopedPlatformHandle platform_handle) {
  NOTIMPLEMENTED();
  return ScopedMessagePipeHandle();
}

void CreateParentMessagePipe(
    const std::string& token,
    const base::Callback<void(ScopedMessagePipeHandle)>& callback) {
  DCHECK(internal::g_core);
  internal::g_core->CreateParentMessagePipe(token, callback);
}

void CreateChildMessagePipe(
    const std::string& token,
    const base::Callback<void(ScopedMessagePipeHandle)>& callback) {
  DCHECK(internal::g_core);
  internal::g_core->CreateChildMessagePipe(token, callback);
}

}  // namespace edk
}  // namespace mojo
