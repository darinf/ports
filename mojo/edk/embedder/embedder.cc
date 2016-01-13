// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/edk/embedder/embedder.h"

#include <stdint.h>

#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/strings/string_number_conversions.h"
#include "crypto/random.h"
#include "mojo/edk/embedder/embedder_internal.h"
#include "mojo/edk/embedder/platform_channel_pair.h"
#include "mojo/edk/embedder/simple_platform_support.h"
#include "mojo/edk/system/core.h"

namespace mojo {
namespace edk {

class Core;
class PlatformSupport;

namespace internal {

Core* g_core;
base::TaskRunner* g_io_thread_task_runner;
PlatformSupport* g_platform_support;

// This is used to help negotiate message pipe connections over arbitrary
// platform channels. The the embedder needs to know which end of the pipe it's
// on so it can do the right thing.
//
// TODO: Remove this when people stop using mojo::embedder::CreateChannel()
// and thus mojo::edk::CreateMessagePipe(ScopedPlatformHandle).
bool g_is_parent_process = true;

Core* GetCore() { return g_core; }

}  // namespace internal

void SetMaxMessageSize(size_t bytes) {
}

void PreInitializeParentProcess() {
}

void PreInitializeChildProcess() {
  internal::g_is_parent_process = false;
}

ScopedPlatformHandle ChildProcessLaunched(base::ProcessHandle child_process) {
  PlatformChannelPair channel;
  ChildProcessLaunched(child_process, channel.PassServerHandle());
  return channel.PassClientHandle();
}

void ChildProcessLaunched(base::ProcessHandle child_process,
                          ScopedPlatformHandle server_pipe) {
  CHECK(internal::g_core);
  internal::g_core->AddChild(std::move(server_pipe));
}

void SetParentPipeHandle(ScopedPlatformHandle pipe) {
  CHECK(internal::g_core);
  internal::g_is_parent_process = false;
  internal::g_core->InitChild(std::move(pipe));
}

void Init() {
  internal::g_core = new Core();
  internal::g_platform_support = new SimplePlatformSupport();
}

MojoResult AsyncWait(MojoHandle handle,
                     MojoHandleSignals signals,
                     const base::Callback<void(MojoResult)>& callback) {
  CHECK(internal::g_core);
  return internal::g_core->AsyncWait(handle, signals, callback);
}

MojoResult CreatePlatformHandleWrapper(
    ScopedPlatformHandle platform_handle,
    MojoHandle* platform_handle_wrapper_handle) {
  return internal::g_core->CreatePlatformHandleWrapper(
      std::move(platform_handle), platform_handle_wrapper_handle);
}

MojoResult PassWrappedPlatformHandle(MojoHandle platform_handle_wrapper_handle,
                                     ScopedPlatformHandle* platform_handle) {
  return internal::g_core->PassWrappedPlatformHandle(
      platform_handle_wrapper_handle, platform_handle);
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
}

ScopedMessagePipeHandle CreateMessagePipe(
    ScopedPlatformHandle platform_handle) {
  DCHECK(internal::g_core);
  if (internal::g_is_parent_process) {
    return internal::g_core->CreateParentMessagePipe(
        std::move(platform_handle));
  } else {
    return internal::g_core->CreateChildMessagePipe(std::move(platform_handle));
  }
}

ScopedMessagePipeHandle CreateParentMessagePipe(const std::string& token) {
  DCHECK(internal::g_core);
  return internal::g_core->CreateParentMessagePipe(token);
}

ScopedMessagePipeHandle CreateChildMessagePipe(const std::string& token) {
  DCHECK(internal::g_core);
  return internal::g_core->CreateChildMessagePipe(token);
}

std::string GenerateRandomToken() {
  char random_bytes[16];
  crypto::RandBytes(random_bytes, 16);
  return base::HexEncode(random_bytes, 16);
}

}  // namespace edk
}  // namespace mojo
