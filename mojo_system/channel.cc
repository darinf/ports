// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ports/mojo_system/channel.h"

namespace mojo {
namespace edk {

Channel::Channel() {}

Channel::IncomingMessage::IncomingMessage(const void* data,
                                          size_t num_bytes,
                                          ScopedPlatformHandleVectorPtr handles)
    : data_(data), num_bytes_(num_bytes), handles_(std::move(handles)) {}

Channel::IncomingMessage::~IncomingMessage() {}

Channel::OutgoingMessage::OutgoingMessage(std::vector<char> data,
                                          ScopedPlatformHandleVectorPtr handles)
    : data_(std::move(data)), handles_(std::move(handles)) {}

Channel::OutgoingMessage::~OutgoingMessage() {}

}  // namespace edk
}  // namespace mojo
