// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ports/mojo_system/node_channel.h"

#include "base/logging.h"
#include "ports/mojo_system/channel.h"

namespace mojo {
namespace edk {

NodeChannel::NodeChannel(ScopedPlatformHandle platform_handle,
                         scoped_refptr<base::TaskRunner> io_task_runner)
    : channel_(
        Channel::Create(this, std::move(platform_handle), io_task_runner)) {}

NodeChannel::~NodeChannel() {}

void NodeChannel::OnChannelRead(Channel::IncomingMessage* message) {
  NOTIMPLEMENTED();
}

void NodeChannel::OnChannelError() {
  NOTIMPLEMENTED();
}

}  // namespace edk
}  // namespace mojo
