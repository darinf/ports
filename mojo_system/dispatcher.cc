// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ports/mojo_system/dispatcher.h"

#include "base/logging.h"
#include "mojo/edk/system/configuration.h"

namespace mojo {
namespace edk {

Dispatcher::DispatcherInTransit::DispatcherInTransit() {}

Dispatcher::DispatcherInTransit::~DispatcherInTransit() {}

MojoResult Dispatcher::Close() {
  base::AutoLock locker(lock_);
  if (is_closed_)
    return MOJO_RESULT_INVALID_ARGUMENT;

  CloseNoLock();
  return MOJO_RESULT_OK;
}

MojoResult Dispatcher::WriteMessage(const void* bytes,
                                    uint32_t num_bytes,
                                    const DispatcherInTransit* dispatchers,
                                    uint32_t num_dispatchers,
                                    MojoWriteMessageFlags flags) {
  base::AutoLock locker(lock_);
  if (is_closed_)
    return MOJO_RESULT_INVALID_ARGUMENT;
  return WriteMessageImplNoLock(bytes, num_bytes, dispatchers, num_dispatchers,
                                flags);
}

MojoResult Dispatcher::ReadMessage(void* bytes,
                                   uint32_t* num_bytes,
                                   MojoHandle* handles,
                                   uint32_t* num_handles,
                                   MojoReadMessageFlags flags) {
  base::AutoLock locker(lock_);
  if (is_closed_)
    return MOJO_RESULT_INVALID_ARGUMENT;
  return ReadMessageImplNoLock(bytes, num_bytes, handles, num_handles, flags);
}

MojoResult Dispatcher::DuplicateBufferHandle(
    const MojoDuplicateBufferHandleOptions* options,
    scoped_refptr<Dispatcher>* new_dispatcher) {
  base::AutoLock locker(lock_);
  if (is_closed_)
    return MOJO_RESULT_INVALID_ARGUMENT;

  return DuplicateBufferHandleImplNoLock(options, new_dispatcher);
}

MojoResult Dispatcher::MapBuffer(
    uint64_t offset,
    uint64_t num_bytes,
    MojoMapBufferFlags flags,
    scoped_ptr<PlatformSharedBufferMapping>* mapping) {
  base::AutoLock locker(lock_);
  if (is_closed_)
    return MOJO_RESULT_INVALID_ARGUMENT;

  return MapBufferImplNoLock(offset, num_bytes, flags, mapping);
}

HandleSignalsState Dispatcher::GetHandleSignalsState() const {
  base::AutoLock locker(lock_);
  if (is_closed_)
    return HandleSignalsState();
  return GetHandleSignalsStateImplNoLock();
}

MojoResult Dispatcher::AddAwakable(Awakable* awakable,
                                   MojoHandleSignals signals,
                                   uintptr_t context,
                                   HandleSignalsState* signals_state) {
  base::AutoLock locker(lock_);
  if (is_closed_) {
    if (signals_state)
      *signals_state = HandleSignalsState();
    return MOJO_RESULT_INVALID_ARGUMENT;
  }
  return AddAwakableImplNoLock(awakable, signals, context, signals_state);
}

void Dispatcher::RemoveAwakable(Awakable* awakable,
                                HandleSignalsState* handle_signals_state) {
  base::AutoLock locker(lock_);
  if (is_closed_) {
    if (handle_signals_state)
      *handle_signals_state = HandleSignalsState();
    return;
  }
  RemoveAwakableImplNoLock(awakable, handle_signals_state);
}

MojoResult Dispatcher::AddWaitingDispatcher(
    const scoped_refptr<Dispatcher>& dispatcher,
    MojoHandleSignals signals,
    uintptr_t context) {
  base::AutoLock locker(lock_);
  if (is_closed_)
    return MOJO_RESULT_INVALID_ARGUMENT;

  return AddWaitingDispatcherImplNoLock(dispatcher, signals, context);
}

MojoResult Dispatcher::RemoveWaitingDispatcher(
    const scoped_refptr<Dispatcher>& dispatcher) {
  base::AutoLock locker(lock_);
  if (is_closed_)
    return MOJO_RESULT_INVALID_ARGUMENT;

  return RemoveWaitingDispatcherImplNoLock(dispatcher);
}

MojoResult Dispatcher::GetReadyDispatchers(uint32_t* count,
                                           DispatcherVector* dispatchers,
                                           MojoResult* results,
                                           uintptr_t* contexts) {
  base::AutoLock locker(lock_);
  if (is_closed_)
    return MOJO_RESULT_INVALID_ARGUMENT;

  return GetReadyDispatchersImplNoLock(count, dispatchers, results, contexts);
}

bool Dispatcher::BeginTransit() {
  return true;
}

void Dispatcher::CompleteTransit() {
}

void Dispatcher::CancelTransit() {
}

Dispatcher::Dispatcher() : is_closed_(false) {
}

Dispatcher::~Dispatcher() {
  // Make sure that |Close()| was called.
  DCHECK(is_closed_);
}

void Dispatcher::CancelAllAwakablesNoLock() {
  lock_.AssertAcquired();
  DCHECK(is_closed_);
  // By default, waiting isn't supported. Only dispatchers that can be waited on
  // will do something nontrivial.
}

void Dispatcher::CloseImplNoLock() {
  lock_.AssertAcquired();
  DCHECK(is_closed_);
  // This may not need to do anything. Dispatchers should override this to do
  // any actual close-time cleanup necessary.
}

MojoResult Dispatcher::WriteMessageImplNoLock(
    const void* bytes,
    uint32_t num_bytes,
    const DispatcherInTransit* dispatchers,
    uint32_t num_dispatchers,
    MojoWriteMessageFlags flags) {
  lock_.AssertAcquired();
  DCHECK(!is_closed_);
  // By default, not supported. Only needed for message pipe dispatchers.
  return MOJO_RESULT_INVALID_ARGUMENT;
}

MojoResult Dispatcher::ReadMessageImplNoLock(void* bytes,
                                             uint32_t* num_bytes,
                                             MojoHandle* handles,
                                             uint32_t* num_handles,
                                             MojoReadMessageFlags flags) {
  lock_.AssertAcquired();
  DCHECK(!is_closed_);
  // By default, not supported. Only needed for message pipe dispatchers.
  return MOJO_RESULT_INVALID_ARGUMENT;
}

MojoResult Dispatcher::DuplicateBufferHandleImplNoLock(
    const MojoDuplicateBufferHandleOptions* options,
    scoped_refptr<Dispatcher>* new_dispatcher) {
  lock_.AssertAcquired();
  DCHECK(!is_closed_);
  return MOJO_RESULT_INVALID_ARGUMENT;
}

MojoResult Dispatcher::MapBufferImplNoLock(
    uint64_t offset,
    uint64_t num_bytes,
    MojoMapBufferFlags flags,
    scoped_ptr<PlatformSharedBufferMapping>* mapping) {
  lock_.AssertAcquired();
  DCHECK(!is_closed_);
  return MOJO_RESULT_INVALID_ARGUMENT;
}

MojoResult Dispatcher::AddAwakableImplNoLock(
    Awakable* /*awakable*/,
    MojoHandleSignals /*signals*/,
    uintptr_t /*context*/,
    HandleSignalsState* signals_state) {
  lock_.AssertAcquired();
  DCHECK(!is_closed_);
  // By default, waiting isn't supported. Only dispatchers that can be waited on
  // will do something nontrivial.
  if (signals_state)
    *signals_state = HandleSignalsState();
  return MOJO_RESULT_FAILED_PRECONDITION;
}

void Dispatcher::RemoveAwakableImplNoLock(Awakable* /*awakable*/,
                                          HandleSignalsState* signals_state) {
  lock_.AssertAcquired();
  DCHECK(!is_closed_);
  // By default, waiting isn't supported. Only dispatchers that can be waited on
  // will do something nontrivial.
  if (signals_state)
    *signals_state = HandleSignalsState();
}

MojoResult Dispatcher::AddWaitingDispatcherImplNoLock(
    const scoped_refptr<Dispatcher>& dispatcher,
    MojoHandleSignals signals,
    uintptr_t context) {
  lock_.AssertAcquired();
  DCHECK(!is_closed_);
  return MOJO_RESULT_INVALID_ARGUMENT;
}

MojoResult Dispatcher::RemoveWaitingDispatcherImplNoLock(
    const scoped_refptr<Dispatcher>& dispatcher) {
  lock_.AssertAcquired();
  DCHECK(!is_closed_);
  return MOJO_RESULT_INVALID_ARGUMENT;
}

MojoResult Dispatcher::GetReadyDispatchersImplNoLock(
    uint32_t* count,
    DispatcherVector* dispatchers,
    MojoResult* results,
    uintptr_t* contexts) {
  lock_.AssertAcquired();
  DCHECK(!is_closed_);
  return MOJO_RESULT_INVALID_ARGUMENT;
}

HandleSignalsState Dispatcher::GetHandleSignalsStateImplNoLock() const {
  lock_.AssertAcquired();
  DCHECK(!is_closed_);
  // By default, waiting isn't supported. Only dispatchers that can be waited on
  // will do something nontrivial.
  return HandleSignalsState();
}

bool Dispatcher::IsBusyNoLock() const {
  lock_.AssertAcquired();
  DCHECK(!is_closed_);
  // Most dispatchers support only "atomic" operations, so they are never busy
  // (in this sense).
  return false;
}

void Dispatcher::CloseNoLock() {
  lock_.AssertAcquired();
  DCHECK(!is_closed_);

  is_closed_ = true;
  CancelAllAwakablesNoLock();
  CloseImplNoLock();
}

}  // namespace edk
}  // namespace mojo
