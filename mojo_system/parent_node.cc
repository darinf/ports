// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ports/mojo_system/parent_node.h"

#include "base/logging.h"

namespace mojo {
namespace edk {

ParentNode::ParentNode() {
  GenerateRandomName(&name_);

  DLOG(INFO) << "Initializing parent node " << name_;
}

ParentNode::~ParentNode() {}

void ParentNode::AddChild(ScopedPlatformHandle platform_handle,
                          scoped_refptr<base::TaskRunner> io_task_runner) {
  ports::NodeName child_name;
  GenerateRandomName(&child_name);

  DLOG(INFO) << "Adding child node " << child_name;

  scoped_ptr<NodeChannel> channel(new NodeChannel(
      this, std::move(platform_handle), io_task_runner));
  channel->SetRemoteNodeName(child_name);
  channel->SendMessage(NodeChannel::Message::NewInitializeChildMessage(
      name_, child_name));
  children_.insert(std::make_pair(child_name, std::move(channel)));
}

void ParentNode::OnMessageReceived(const ports::NodeName& node,
                                   NodeChannel::MessagePtr message) {
  NOTIMPLEMENTED();
}

void ParentNode::OnChannelError(const ports::NodeName& node) {
  NOTIMPLEMENTED();
}


}  // namespace edk
}  // namespace mojo
