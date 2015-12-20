// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PORTS_MOJO_SYSTEM_NODE_CHANNEL_H_
#define PORTS_MOJO_SYSTEM_NODE_CHANNEL_H_

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/task_runner.h"
#include "mojo/edk/embedder/scoped_platform_handle.h"
#include "ports/mojo_system/channel.h"

namespace mojo {
namespace edk {

class NodeChannel : public Channel::Delegate {
 public:
  NodeChannel(ScopedPlatformHandle platform_handle,
              scoped_refptr<base::TaskRunner> io_task_runner);
  ~NodeChannel() override;

 private:
  // Channel::Delegate:
  void OnChannelRead(Channel::IncomingMessage* message) override;
  void OnChannelError() override;

  scoped_refptr<Channel> channel_;

  DISALLOW_COPY_AND_ASSIGN(NodeChannel);
};

}  // namespace edk
}  // namespace mojo

#endif  // PORTS_MOJO_SYSTEM_NODE_CHANNEL_H_
