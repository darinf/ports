// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/edk/system/ports_message.h"

#include "mojo/edk/system/node_channel.h"

namespace mojo {
namespace edk {

PortsMessage::PortsMessage(size_t num_header_bytes,
                           size_t num_payload_bytes,
                           size_t num_ports_bytes,
                           const void* bytes,
                           size_t num_bytes,
                           ScopedPlatformHandleVectorPtr platform_handles)
    : ports::Message(num_header_bytes,
                     num_payload_bytes,
                     num_ports_bytes) {
  size_t size = num_header_bytes + num_payload_bytes + num_ports_bytes;

  void* ptr;
  channel_message_ = NodeChannel::CreatePortsMessage(
      size, &ptr, std::move(platform_handles));

  start_ = static_cast<char*>(ptr);
  if (bytes) {
    DCHECK_EQ(num_bytes,
              num_header_bytes + num_payload_bytes + num_ports_bytes);
    memcpy(start_, bytes, num_bytes);
  }
}

PortsMessage::~PortsMessage() {}

}  // namespace edk
}  // namespace mojo
