// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_EDK_SYSTEM_PORTS_MESSAGE_H__
#define MOJO_EDK_SYSTEM_PORTS_MESSAGE_H__

#include <utility>

#include "mojo/edk/embedder/platform_handle_vector.h"
#include "mojo/edk/system/channel.h"
#include "mojo/edk/system/ports/message.h"

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

  PlatformHandle* handles() { return channel_message_->handles(); }
  size_t num_handles() const { return channel_message_->num_handles(); }

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

#endif  // MOJO_EDK_SYSTEM_PORTS_MESSAGE_H__
