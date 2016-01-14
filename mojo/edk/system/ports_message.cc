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
                           Channel::MessagePtr channel_message)
    : ports::Message(num_header_bytes,
                     num_payload_bytes,
                     num_ports_bytes) {
  if (channel_message) {
    channel_message_ = std::move(channel_message);

    void* data;
    size_t num_data_bytes;
    NodeChannel::GetPortsMessageData(channel_message_.get(), &data,
                                     &num_data_bytes);
    start_ = static_cast<char*>(data);
  } else {
    size_t size = num_header_bytes + num_payload_bytes + num_ports_bytes;
    void* ptr;
    channel_message_ = NodeChannel::CreatePortsMessage(size, &ptr, nullptr);
    start_ = static_cast<char*>(ptr);
  }
}

PortsMessage::~PortsMessage() {}

}  // namespace edk
}  // namespace mojo
