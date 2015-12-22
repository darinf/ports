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

HandleSignalsState Dispatcher::GetHandleSignalsStateImplNoLock() const {
  lock_.AssertAcquired();
  DCHECK(!is_closed_);
  // By default, waiting isn't supported. Only dispatchers that can be waited on
  // will do something nontrivial.
  return HandleSignalsState();
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
