// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ports/mojo_system/shared_buffer_dispatcher.h"

#include <stddef.h>
#include <stdint.h>

#include <limits>
#include <utility>

#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "mojo/edk/embedder/embedder_internal.h"
#include "mojo/edk/embedder/platform_support.h"
#include "mojo/edk/system/configuration.h"
#include "mojo/edk/system/options_validation.h"
#include "mojo/public/c/system/macros.h"

namespace mojo {
namespace edk {

namespace {

struct MOJO_ALIGNAS(8) SerializedSharedBufferDispatcher {
  size_t num_bytes;
};

}  // namespace

// static
const MojoCreateSharedBufferOptions
    SharedBufferDispatcher::kDefaultCreateOptions = {
        static_cast<uint32_t>(sizeof(MojoCreateSharedBufferOptions)),
        MOJO_CREATE_SHARED_BUFFER_OPTIONS_FLAG_NONE};

// static
MojoResult SharedBufferDispatcher::ValidateCreateOptions(
    const MojoCreateSharedBufferOptions* in_options,
    MojoCreateSharedBufferOptions* out_options) {
  const MojoCreateSharedBufferOptionsFlags kKnownFlags =
      MOJO_CREATE_SHARED_BUFFER_OPTIONS_FLAG_NONE;

  *out_options = kDefaultCreateOptions;
  if (!in_options)
    return MOJO_RESULT_OK;

  UserOptionsReader<MojoCreateSharedBufferOptions> reader(in_options);
  if (!reader.is_valid())
    return MOJO_RESULT_INVALID_ARGUMENT;

  if (!OPTIONS_STRUCT_HAS_MEMBER(MojoCreateSharedBufferOptions, flags, reader))
    return MOJO_RESULT_OK;
  if ((reader.options().flags & ~kKnownFlags))
    return MOJO_RESULT_UNIMPLEMENTED;
  out_options->flags = reader.options().flags;

  // Checks for fields beyond |flags|:

  // (Nothing here yet.)

  return MOJO_RESULT_OK;
}

// static
MojoResult SharedBufferDispatcher::Create(
    PlatformSupport* platform_support,
    const MojoCreateSharedBufferOptions& /*validated_options*/,
    uint64_t num_bytes,
    scoped_refptr<SharedBufferDispatcher>* result) {
  if (!num_bytes)
    return MOJO_RESULT_INVALID_ARGUMENT;
  if (num_bytes > GetConfiguration().max_shared_memory_num_bytes)
    return MOJO_RESULT_RESOURCE_EXHAUSTED;

  scoped_refptr<PlatformSharedBuffer> shared_buffer(
      platform_support->CreateSharedBuffer(static_cast<size_t>(num_bytes)));
  if (!shared_buffer)
    return MOJO_RESULT_RESOURCE_EXHAUSTED;

  *result = CreateInternal(std::move(shared_buffer));
  return MOJO_RESULT_OK;
}

Dispatcher::Type SharedBufferDispatcher::GetType() const {
  return Type::SHARED_BUFFER;
}

// static
scoped_refptr<SharedBufferDispatcher> SharedBufferDispatcher::Deserialize(
    const void* bytes,
    size_t num_bytes,
    PlatformHandle* platform_handles,
    size_t num_platform_handles) {
  if (num_bytes != sizeof(SerializedSharedBufferDispatcher)) {
    LOG(ERROR) << "Invalid serialized shared buffer dispatcher (bad size)";
    return nullptr;
  }

  const SerializedSharedBufferDispatcher* serialization =
      static_cast<const SerializedSharedBufferDispatcher*>(bytes);
  if (!serialization->num_bytes) {
    LOG(ERROR)
        << "Invalid serialized shared buffer dispatcher (invalid num_bytes)";
    return nullptr;
  }

  if (!platform_handles || num_platform_handles != 1) {
    LOG(ERROR)
        << "Invalid serialized shared buffer dispatcher (missing handles)";
    return nullptr;
  }

  // Starts off invalid, which is what we want.
  PlatformHandle platform_handle;
  // We take ownership of the handle, so we have to invalidate the one in
  // |platform_handles|.
  std::swap(platform_handle, *platform_handles);

  // Wrapping |platform_handle| in a |ScopedPlatformHandle| means that it'll be
  // closed even if creation fails.
  scoped_refptr<PlatformSharedBuffer> shared_buffer(
      internal::g_platform_support->CreateSharedBufferFromHandle(
          serialization->num_bytes, ScopedPlatformHandle(platform_handle)));
  if (!shared_buffer) {
    LOG(ERROR)
        << "Invalid serialized shared buffer dispatcher (invalid num_bytes?)";
    return nullptr;
  }

  return CreateInternal(std::move(shared_buffer));
}

SharedBufferDispatcher::SharedBufferDispatcher(
    scoped_refptr<PlatformSharedBuffer> shared_buffer)
    : shared_buffer_(shared_buffer) {
  DCHECK(shared_buffer_);
}

SharedBufferDispatcher::~SharedBufferDispatcher() {
}

// static
MojoResult SharedBufferDispatcher::ValidateDuplicateOptions(
    const MojoDuplicateBufferHandleOptions* in_options,
    MojoDuplicateBufferHandleOptions* out_options) {
  const MojoDuplicateBufferHandleOptionsFlags kKnownFlags =
      MOJO_DUPLICATE_BUFFER_HANDLE_OPTIONS_FLAG_NONE;
  static const MojoDuplicateBufferHandleOptions kDefaultOptions = {
      static_cast<uint32_t>(sizeof(MojoDuplicateBufferHandleOptions)),
      MOJO_DUPLICATE_BUFFER_HANDLE_OPTIONS_FLAG_NONE};

  *out_options = kDefaultOptions;
  if (!in_options)
    return MOJO_RESULT_OK;

  UserOptionsReader<MojoDuplicateBufferHandleOptions> reader(in_options);
  if (!reader.is_valid())
    return MOJO_RESULT_INVALID_ARGUMENT;

  if (!OPTIONS_STRUCT_HAS_MEMBER(MojoDuplicateBufferHandleOptions, flags,
                                 reader))
    return MOJO_RESULT_OK;
  if ((reader.options().flags & ~kKnownFlags))
    return MOJO_RESULT_UNIMPLEMENTED;
  out_options->flags = reader.options().flags;

  // Checks for fields beyond |flags|:

  // (Nothing here yet.)

  return MOJO_RESULT_OK;
}

void SharedBufferDispatcher::CloseImplNoLock() {
  lock().AssertAcquired();
  DCHECK(shared_buffer_);
  shared_buffer_ = nullptr;
}

MojoResult SharedBufferDispatcher::DuplicateBufferHandleImplNoLock(
    const MojoDuplicateBufferHandleOptions* options,
    scoped_refptr<Dispatcher>* new_dispatcher) {
  lock().AssertAcquired();

  MojoDuplicateBufferHandleOptions validated_options;
  MojoResult result = ValidateDuplicateOptions(options, &validated_options);
  if (result != MOJO_RESULT_OK)
    return result;

  // Note: Since this is "duplicate", we keep our ref to |shared_buffer_|.
  *new_dispatcher = CreateInternal(shared_buffer_);
  return MOJO_RESULT_OK;
}

MojoResult SharedBufferDispatcher::MapBufferImplNoLock(
    uint64_t offset,
    uint64_t num_bytes,
    MojoMapBufferFlags flags,
    scoped_ptr<PlatformSharedBufferMapping>* mapping) {
  lock().AssertAcquired();
  DCHECK(shared_buffer_);

  if (offset > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
    return MOJO_RESULT_INVALID_ARGUMENT;
  if (num_bytes > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
    return MOJO_RESULT_INVALID_ARGUMENT;

  if (!shared_buffer_->IsValidMap(static_cast<size_t>(offset),
                                  static_cast<size_t>(num_bytes)))
    return MOJO_RESULT_INVALID_ARGUMENT;

  DCHECK(mapping);
  *mapping = shared_buffer_->MapNoCheck(static_cast<size_t>(offset),
                                        static_cast<size_t>(num_bytes));
  if (!*mapping)
    return MOJO_RESULT_RESOURCE_EXHAUSTED;

  return MOJO_RESULT_OK;
}

void SharedBufferDispatcher::GetSerializedSizeImplNoLock(
    uint32_t* num_bytes,
    uint32_t* num_platform_handles) {
  *num_bytes = sizeof(SerializedSharedBufferDispatcher);
  *num_platform_handles = 1;
}

bool SharedBufferDispatcher::SerializeAndCloseImplNoLock(
    void* destination,
    PlatformHandleVector* handles) {
  SerializedSharedBufferDispatcher* serialization =
      static_cast<SerializedSharedBufferDispatcher*>(destination);
  serialization->num_bytes = shared_buffer_->GetNumBytes();
  ScopedPlatformHandle handle(
      shared_buffer_->HasOneRef() ? shared_buffer_->PassPlatformHandle()
                                  : shared_buffer_->DuplicatePlatformHandle());
  if (!handle.is_valid()) {
    shared_buffer_ = nullptr;
    return false;
  }

  handles->push_back(handle.release());

  CloseNoLock();
  return true;
}

}  // namespace edk
}  // namespace mojo
