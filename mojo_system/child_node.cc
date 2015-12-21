// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ports/mojo_system/child_node.h"

#include "base/logging.h"

namespace mojo {
namespace edk {

ChildNode::ChildNode(ScopedPlatformHandle platform_handle,
                     scoped_refptr<base::TaskRunner> io_task_runner)
    : bootstrap_channel_(
          new NodeChannel(this, std::move(platform_handle), io_task_runner)) {}

ChildNode::~ChildNode() {}

void ChildNode::OnMessageReceived(const ports::NodeName& node,
                                  NodeChannel::MessagePtr message) {
  if (bootstrap_channel_) {
    // Anticipate receiving our first message from the parent node. It must be
    // a HELLO_CHILD message.
    DCHECK(node == ports::kInvalidNodeName);
    DCHECK(message->type() == NodeChannel::Message::Type::HELLO_CHILD);

    const auto& data = message->AsHelloChild();
    parent_name_ = data.parent_name;
    bootstrap_channel_->SendMessage(NodeChannel::Message::NewHelloParentMessage(
        data.token_name, name()));

    AddPeer(parent_name_, std::move(bootstrap_channel_));

    DLOG(INFO) << "Initializing child with name " << name() << " and parent "
        << parent_name_;
    return;
  }

  switch (message->type()) {
    case NodeChannel::Message::Type::HELLO_CHILD:
      NOTREACHED() << "Unexpected HELLO_CHILD message.";
      break;

    case NodeChannel::Message::Type::HELLO_PARENT:
      NOTREACHED() << "Unexpected HELLO_PARENT message.";
      break;

    case NodeChannel::Message::Type::EVENT: {
      const auto& data = message->AsEvent();
      DLOG(INFO) << "Received an event for port " << data.port_name;
      if (data.type == ports::Event::kAcceptMessage) {
        DLOG(INFO) << "It's a message with " << data.message->num_bytes <<
            " bytes and " << data.message->num_ports << " ports.";
      }
      break;
    }

    default:
      NOTREACHED() << "Unknown message type.";
  }
}

void ChildNode::OnChannelError(const ports::NodeName& node) {
  NOTREACHED() << "Broken pipe!";
}

}  // namespace edk
}  // namespace mojo
