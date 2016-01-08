// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/edk/system/platform_handle_dispatcher.h"

#include "base/synchronization/lock.h"
#include "mojo/edk/embedder/platform_handle_vector.h"

namespace mojo {
namespace edk {

// static
scoped_refptr<PlatformHandleDispatcher> PlatformHandleDispatcher::Create(
    ScopedPlatformHandle platform_handle) {
  return new PlatformHandleDispatcher(std::move(platform_handle));
}

ScopedPlatformHandle PlatformHandleDispatcher::PassPlatformHandle() {
  return std::move(platform_handle_);
}

Dispatcher::Type PlatformHandleDispatcher::GetType() const {
  return Type::PLATFORM_HANDLE;
}

MojoResult PlatformHandleDispatcher::Close() {
  base::AutoLock lock(lock_);
  if (is_closed_)
    return MOJO_RESULT_INVALID_ARGUMENT;
  is_closed_ = true;
  platform_handle_.reset();
  return MOJO_RESULT_OK;
}

void PlatformHandleDispatcher::StartSerialize(uint32_t* num_bytes,
                                              uint32_t* num_handles) {
  *num_bytes = 0;
  *num_handles = 1;
}

bool PlatformHandleDispatcher::EndSerializeAndClose(
    void* destination,
    PlatformHandleVector* handles) {
  base::AutoLock lock(lock_);
  if (is_closed_)
    return false;
  handles->push_back(platform_handle_.release());
  is_closed_ = true;
  return true;
}

// static
scoped_refptr<PlatformHandleDispatcher> PlatformHandleDispatcher::Deserialize(
    const void* bytes,
    size_t num_bytes,
    PlatformHandle* handles,
    size_t num_handles) {
  if (num_bytes)
    return nullptr;
  if (num_handles != 1)
    return nullptr;

  PlatformHandle handle;
  std::swap(handle, handles[0]);

  return PlatformHandleDispatcher::Create(ScopedPlatformHandle(handle));
}

PlatformHandleDispatcher::PlatformHandleDispatcher(
    ScopedPlatformHandle platform_handle)
    : platform_handle_(std::move(platform_handle)) {}

PlatformHandleDispatcher::~PlatformHandleDispatcher() {
  DCHECK(is_closed_);
  DCHECK(!platform_handle_.is_valid());
}

}  // namespace edk
}  // namespace mojo
