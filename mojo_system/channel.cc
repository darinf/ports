// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ports/mojo_system/channel.h"

#include <cstring>

namespace mojo {
namespace edk {

namespace {

static_assert(sizeof(Channel::Message::Header) % kChannelMessageAlignment == 0,
    "Invalid Header size.");

}  // namespace

const size_t kReadBufferSize = 4096;
const size_t kMaxUnusedReadBufferCapacity = 256 * 1024;
const size_t kMaxChannelMessageSize = 256 * 1024 * 1024;

Channel::Message::Message(size_t payload_size,
                          ScopedPlatformHandleVectorPtr handles)
    : handles_(std::move(handles)) {
  size_ = payload_size + sizeof(Header);
  data_ = static_cast<char*>(malloc(size_));
  Header* header = reinterpret_cast<Header*>(data_);
  header->num_bytes = size_;
  header->num_handles = handles_ ? handles_->size() : 0;
  header->padding = 0;
}

Channel::Message::~Message() {
  free(data_);
}

void Channel::Message::SetHandles(ScopedPlatformHandleVectorPtr handles) {
  header()->num_handles = handles ? handles->size() : 0;
  std::swap(handles, handles_);
}

Channel::Channel(Delegate* delegate)
    : delegate_(delegate) {
  read_buffer_size_ = kReadBufferSize;
  read_buffer_ = static_cast<char*>(malloc(read_buffer_size_));
}

Channel::~Channel() {
  free(read_buffer_);
}

void Channel::ShutDown() {
  delegate_ = nullptr;
  ShutDownImpl();
}

char* Channel::GetReadBuffer(size_t *buffer_capacity) {
  const size_t required_capacity = kReadBufferSize;

  DCHECK_GE(read_buffer_size_, num_read_bytes_);
  DCHECK_GE(num_read_bytes_, read_offset_);

  if (read_offset_ > kMaxUnusedReadBufferCapacity) {
    // Shift outstanding data to the front of the buffer and shrink to a
    // reasonable size, leaving enough space for another read. This will slow
    // down very large reads, but we shouldn't do very large reads.
    std::move(read_buffer_ + read_offset_, read_buffer_ + num_read_bytes_,
              read_buffer_);
    read_buffer_size_ = num_read_bytes_ - read_offset_ + required_capacity;
    read_buffer_ = static_cast<char*>(realloc(read_buffer_, read_buffer_size_));
    num_read_bytes_ -= read_offset_;
    read_offset_ = 0;
  } else {
    if (read_buffer_size_ - num_read_bytes_ < required_capacity) {
      // Grow the buffer as needed. This resizes it to either twice its previous
      // size, or just enough to hold the new capacity; whichever is larger.
      read_buffer_size_ = std::max(read_buffer_size_ * 2,
                                   num_read_bytes_ + required_capacity);
      read_buffer_ = static_cast<char*>(realloc(read_buffer_,
                                                read_buffer_size_));
    }
  }

  DCHECK_GE(read_buffer_size_, num_read_bytes_ + required_capacity);

  *buffer_capacity = required_capacity;
  return read_buffer_ + num_read_bytes_;
}

bool Channel::OnReadCompleteNoLock(size_t bytes_read) {
  read_lock().AssertAcquired();

  num_read_bytes_ += bytes_read;
  while (num_read_bytes_ - read_offset_ >= sizeof(Message::Header)) {
    // We have at least enough data available for a MessageHeader.
    Message::Header* header =
        reinterpret_cast<Message::Header*>(read_buffer_ + read_offset_);
    if (header->num_bytes < sizeof(Message::Header) ||
        header->num_bytes > kMaxChannelMessageSize) {
      LOG(ERROR) << "Invalid message size: " << header->num_bytes;
      return false;
    }

    if (num_read_bytes_ - read_offset_ < header->num_bytes) {
      // Not enough data available to read the full message.
      break;
    }

    ScopedPlatformHandleVectorPtr handles;
    if (header->num_handles > 0) {
      handles = GetReadPlatformHandlesNoLock(header->num_handles);
      if (!handles) {
        // Not enough handles available for this message.
        break;
      }
    }

    // We've got a complete message! Dispatch it and try another.
    const size_t payload_size = header->num_bytes - sizeof(Message::Header);
    const void* payload = payload_size ? &header[1] : nullptr;
    if (delegate_)
      delegate_->OnChannelMessage(payload, payload_size, std::move(handles));

    read_offset_ += header->num_bytes;
  }

  return true;
}

void Channel::OnError() {
  if (delegate_)
    delegate_->OnChannelError();
  ShutDown();
}

}  // namespace edk
}  // namespace mojo
