// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ports/mojo_system/dispatcher.h"

#include "base/logging.h"
#include "mojo/edk/system/configuration.h"
#include "ports/mojo_system/shared_buffer_dispatcher.h"

namespace mojo {
namespace edk {

Dispatcher::DispatcherInTransit::DispatcherInTransit() {}

Dispatcher::DispatcherInTransit::~DispatcherInTransit() {}

MojoResult Dispatcher::WriteMessage(const void* bytes,
                                    uint32_t num_bytes,
                                    const DispatcherInTransit* dispatchers,
                                    uint32_t num_dispatchers,
                                    MojoWriteMessageFlags flags) {
  return MOJO_RESULT_INVALID_ARGUMENT;
}

MojoResult Dispatcher::ReadMessage(void* bytes,
                                   uint32_t* num_bytes,
                                   MojoHandle* handles,
                                   uint32_t* num_handles,
                                   MojoReadMessageFlags flags) {
  return MOJO_RESULT_INVALID_ARGUMENT;
}

MojoResult Dispatcher::DuplicateBufferHandle(
    const MojoDuplicateBufferHandleOptions* options,
    scoped_refptr<Dispatcher>* new_dispatcher) {
  return MOJO_RESULT_INVALID_ARGUMENT;
}

MojoResult Dispatcher::MapBuffer(
    uint64_t offset,
    uint64_t num_bytes,
    MojoMapBufferFlags flags,
    scoped_ptr<PlatformSharedBufferMapping>* mapping) {
  return MOJO_RESULT_INVALID_ARGUMENT;
}

MojoResult Dispatcher::AddWaitingDispatcher(
    const scoped_refptr<Dispatcher>& dispatcher,
    MojoHandleSignals signals,
    uintptr_t context) {
  return MOJO_RESULT_INVALID_ARGUMENT;
}

MojoResult Dispatcher::RemoveWaitingDispatcher(
    const scoped_refptr<Dispatcher>& dispatcher) {
  return MOJO_RESULT_INVALID_ARGUMENT;
}

MojoResult Dispatcher::GetReadyDispatchers(uint32_t* count,
                                           DispatcherVector* dispatchers,
                                           MojoResult* results,
                                           uintptr_t* contexts) {
  return MOJO_RESULT_INVALID_ARGUMENT;
}

HandleSignalsState Dispatcher::GetHandleSignalsState() const {
  return HandleSignalsState();
}

MojoResult Dispatcher::AddAwakable(Awakable* awakable,
                                   MojoHandleSignals signals,
                                   uintptr_t context,
                                   HandleSignalsState* signals_state) {
  return MOJO_RESULT_INVALID_ARGUMENT;
}

void Dispatcher::RemoveAwakable(Awakable* awakable,
                                HandleSignalsState* handle_signals_state) {
  NOTREACHED();
}

void Dispatcher::GetSerializedSize(uint32_t* num_bytes,
                                   uint32_t* num_platform_handles) {
  *num_bytes = 0;
  *num_platform_handles = 0;
}

bool Dispatcher::SerializeAndClose(void* destination,
                                   PlatformHandleVector* handles) {
  LOG(ERROR) << "Attempting to serialize a non-transferrable dispatcher.";
  return true;
}

bool Dispatcher::BeginTransit() {
  return true;
}

void Dispatcher::CompleteTransit() {
}

void Dispatcher::CancelTransit() {
}

// static
scoped_refptr<Dispatcher> Dispatcher::Deserialize(
    Type type,
    const void* bytes,
    size_t num_bytes,
    PlatformHandle* platform_handles,
    size_t num_platform_handles) {
  switch (type) {
    case Type::SHARED_BUFFER:
      return SharedBufferDispatcher::Deserialize(
          bytes, num_bytes, platform_handles, num_platform_handles);
    default:
      LOG(ERROR) << "Deserializing invalid dispatcher type.";
      return nullptr;
  }
}

Dispatcher::Dispatcher() {}

Dispatcher::~Dispatcher() {}

}  // namespace edk
}  // namespace mojo
