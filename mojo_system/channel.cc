// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ports/mojo_system/channel.h"

#include <cstring>

namespace mojo {
namespace edk {

const size_t kReadBufferSize = 4096;
const size_t kMaxUnusedReadBufferCapacity = 65536;
const size_t kMaxChannelMessageSize = 256 * 1024 * 1024;

Channel::ReadBuffer::ReadBuffer() : data_(kReadBufferSize) {}

Channel::ReadBuffer::~ReadBuffer() {}

void Channel::ReadBuffer::GetBuffer(char** addr, size_t* bytes_to_read) {
  DCHECK_GE(data_.size(), num_valid_bytes_ + kReadBufferSize);
  *addr = &data_[num_valid_bytes_];
  *bytes_to_read = kReadBufferSize;
}

Channel::IncomingMessage::IncomingMessage(const void* data,
                                          ScopedPlatformHandleVectorPtr handles)
    : header_(static_cast<const MessageHeader*>(data)),
      handles_(std::move(handles)) {}

Channel::IncomingMessage::~IncomingMessage() {}

bool Channel::IncomingMessage::IsValid() const {
  return header_->num_bytes <= kMaxChannelMessageSize &&
      header_->num_bytes >= sizeof(MessageHeader) &&
      ((!header_->num_handles && !handles_) ||
        (handles_ && handles_->size() == header_->num_handles));
}

Channel::OutgoingMessage::OutgoingMessage(const void* payload,
                                          size_t payload_size,
                                          ScopedPlatformHandleVectorPtr handles)
    : data_(sizeof(MessageHeader) + payload_size),
      header_(reinterpret_cast<MessageHeader*>(data_.data())),
      handles_(std::move(handles)) {
  header_->num_bytes = data_.size();
  header_->num_handles = handles_ ? handles_->size() : 0;
  header_->padding = 0;

  if (payload)
    memcpy(data_.data() + sizeof(MessageHeader), payload, payload_size);
  else
    memset(data_.data() + sizeof(MessageHeader), 0, payload_size);
}

Channel::OutgoingMessage::~OutgoingMessage() {}

Channel::Channel(Delegate* delegate) : delegate_(delegate) {}

Channel::~Channel() {}

void Channel::OnReadCompleteNoLock(size_t bytes_read) {
  read_lock().AssertAcquired();
  read_buffer_.num_valid_bytes_ += bytes_read;
  size_t data_offset = 0;
  while (read_buffer_.num_valid_bytes_ - data_offset >=
            sizeof(MessageHeader)) {
    MessageHeader* header = reinterpret_cast<MessageHeader*>(
        read_buffer_.data_.data() + data_offset);
    if (read_buffer_.num_valid_bytes_ < header->num_bytes)
      break;
    ScopedPlatformHandleVectorPtr handles;
    if (header->num_handles > 0) {
      handles = GetReadPlatformHandlesNoLock(header->num_handles);
      if (!handles)
        break;
    }
    IncomingMessage message(header, std::move(handles));
    delegate_->OnChannelRead(&message);
    data_offset += header->num_bytes;
  }

  size_t valid_bytes_remaining = read_buffer_.num_valid_bytes_ - data_offset;
  std::copy(read_buffer_.data_.begin() + data_offset,
            read_buffer_.data_.begin() + read_buffer_.num_valid_bytes_,
            read_buffer_.data_.begin());
  read_buffer_.num_valid_bytes_ = valid_bytes_remaining;

  size_t bytes_available =
      read_buffer_.data_.size() - read_buffer_.num_valid_bytes_;

  if (bytes_available < kReadBufferSize) {
    // Ensure that next read has enough space.
    read_buffer_.data_.resize(std::max(read_buffer_.data_.size() * 2,
                                        bytes_available + kReadBufferSize));
  } else if (bytes_available > kMaxUnusedReadBufferCapacity) {
    // Also make sure we occasionally shrink the buffer back down.
    read_buffer_.data_.resize(
        read_buffer_.num_valid_bytes_ + kReadBufferSize);
  }
}

void Channel::OnError() {
  ShutDown();
  delegate_->OnChannelError();
}

}  // namespace edk
}  // namespace mojo
