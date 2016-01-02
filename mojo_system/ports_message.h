// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PORTS_MOJO_SYSTEM_PORTS_MESSAGE_H_
#define PORTS_MOJO_SYSTEM_PORTS_MESSAGE_H_

#include <utility>

#include "mojo/edk/embedder/platform_handle_vector.h"
#include "ports/include/ports.h"
#include "ports/mojo_system/channel.h"

namespace mojo {
namespace edk {

class PortsMessage : public ports::Message {
 public:
  PortsMessage(size_t num_header_bytes,
               size_t num_payload_bytes,
               size_t num_ports_bytes,
               const void* bytes,
               size_t num_bytes,
               ScopedPlatformHandleVectorPtr platform_handles);
  ~PortsMessage() override;

  void SetHandles(ScopedPlatformHandleVectorPtr handles) {
    channel_message_->SetHandles(std::move(handles));
  }

  Channel::MessagePtr TakeChannelMessage() {
    return std::move(channel_message_);
  }

 private:
  Channel::MessagePtr channel_message_;
};

}  // namespace edk
}  // namespace mojo

#endif  // PORTS_MOJO_SYSTEM_PORTS_MESSAGE_H_
