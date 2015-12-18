// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/edk/embedder/embedder.h"

#include "base/logging.h"
#include "ports/mojo_system/core.h"

namespace mojo {
namespace edk {

class Core;
class PlatformSupport;

namespace internal {

Core* g_core;

// Only here to satisfy some messy link dependency in old EDK test code
PlatformSupport* g_platform_support;

}  // namespace internal

void SetMaxMessageSize(size_t bytes) {
  NOTIMPLEMENTED();
}

void PreInitializeParentProcess() {
  NOTIMPLEMENTED();
}

void PreInitializeChildProcess() {
  NOTIMPLEMENTED();
}

ScopedPlatformHandle ChildProcessLaunched(base::ProcessHandle child_process) {
  NOTIMPLEMENTED();
  return ScopedPlatformHandle();
}

void ChildProcessLaunched(base::ProcessHandle child_process,
                          ScopedPlatformHandle server_pipe) {
  NOTIMPLEMENTED();
}

void SetParentPipeHandle(ScopedPlatformHandle pipe) {
  NOTIMPLEMENTED();
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
  NOTIMPLEMENTED();
}

void ShutdownIPCSupportOnIOThread() {
  NOTIMPLEMENTED();
}

void ShutdownIPCSupport() {
  NOTIMPLEMENTED();
}

ScopedMessagePipeHandle CreateMessagePipe(
    ScopedPlatformHandle platform_handle) {
  NOTIMPLEMENTED();
  return ScopedMessagePipeHandle();
}

}  // namespace edk
}  // namespace mojo
