// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ports/mojo_system/parent_node.h"

#include "base/logging.h"
#include "base/memory/scoped_ptr.h"

namespace mojo {
namespace edk {

ParentNode::ParentNode() {}

ParentNode::~ParentNode() {}

void ParentNode::AddChild(ScopedPlatformHandle platform_handle,
                          scoped_refptr<base::TaskRunner> io_task_runner) {
  ports::NodeName token_name;
  GenerateRandomName(&token_name);

  DLOG(INFO) << "Adding child node with token " << token_name;

  // Initiate a handshake with a unique token we can find later.
  scoped_ptr<NodeChannel> channel(new NodeChannel(
      this, std::move(platform_handle), io_task_runner));
  channel->SetRemoteNodeName(token_name);
  channel->SendMessage(NodeChannel::Message::NewHelloChildMessage(
      name(), token_name));

  base::AutoLock lock(pending_handshakes_lock_);
  pending_handshakes_.insert(std::make_pair(token_name, std::move(channel)));
}

void ParentNode::OnMessageReceived(const ports::NodeName& node,
                                   NodeChannel::MessagePtr message) {
  switch (message->type()) {
    case NodeChannel::Message::Type::HELLO_CHILD:
      DLOG(INFO) << "Parent received invalid HELLO_CHILD message.";
      DropPeer(node);
      break;

    case NodeChannel::Message::Type::HELLO_PARENT: {
      auto data = message->AsHelloParent();

      base::AutoLock lock(pending_handshakes_lock_);
      scoped_ptr<NodeChannel> channel;
      auto it = pending_handshakes_.find(node);
      if (it == pending_handshakes_.end()) {
        DLOG(INFO) << "Received unexpected HELLO_PARENT from " << node;
        DropPeer(node);
      }
      else {
        channel = std::move(it->second);
        pending_handshakes_.erase(it);
        if (data.token_name != node) {
          DLOG(INFO) << "Received invalid handshake for token " << node;
        } else {
          DLOG(INFO) << "Accepting handshake for child " << data.child_name
              << " with token " << data.token_name;
          AddPeer(data.child_name, std::move(channel));
        }
      }
      break;
    }

    default:
      NOTREACHED() << "Unknown message type: " << message->type();
  }
}

void ParentNode::OnChannelError(const ports::NodeName& node) {
  NOTIMPLEMENTED();
}


}  // namespace edk
}  // namespace mojo
