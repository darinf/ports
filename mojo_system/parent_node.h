// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PORTS_MOJO_SYSTEM_PARENT_NODE_H_
#define PORTS_MOJO_SYSTEM_PARENT_NODE_H_

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/task_runner.h"
#include "mojo/edk/embedder/scoped_platform_handle.h"
#include "ports/mojo_system/node.h"
#include "ports/mojo_system/node_channel.h"
#include "ports/include/ports.h"

namespace mojo {
namespace edk {

class ParentNode : public Node, public NodeChannel::Delegate {
 public:
  ParentNode();
  ~ParentNode() override;

  void AddChild(ScopedPlatformHandle platform_handle,
                scoped_refptr<base::TaskRunner> io_task_runner);

 private:
  // NodeChannel::Delegate:
  void OnMessageReceived(const ports::NodeName& node,
                         NodeChannel::MessagePtr message) override;
  void OnChannelError(const ports::NodeName& node) override;

  ports::NodeName name_;

  DISALLOW_COPY_AND_ASSIGN(ParentNode);
};

}  // namespace edk
}  // namespace mojo

#endif  // PORTS_MOJO_SYSTEM_PARENT_NODE_H_
