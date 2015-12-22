// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ports/mojo_system/child_node_controller.h"

#include "base/logging.h"
#include "ports/include/ports.h"
#include "ports/mojo_system/node.h"

namespace mojo {
namespace edk {

ChildNodeController::ChildNodeController(Node* node) : node_(node) {
  DCHECK(node_);
}

ChildNodeController::~ChildNodeController() {}

void ChildNodeController::AcceptPeer(const ports::NodeName& peer_name,
                                     scoped_ptr<NodeChannel> channel) {
  if (parent_name_ == ports::kInvalidNodeName) {
    // The parent must be the first peer connection we establish.
    DCHECK(!bootstrap_channel_);
    DCHECK(peer_name == ports::kInvalidNodeName);

    // Start the bootstrap channel. We should receive a HELLO_CHILD from the
    // parent ASAP.
    bootstrap_channel_ = std::move(channel);
    bootstrap_channel_->Start();
    return;
  }

  NOTREACHED() << "Can't accept other child peers yet.";
}

void ChildNodeController::OnPeerLost(const ports::NodeName& name) {
  if (parent_name_ == ports::kInvalidNodeName || parent_name_ == name) {
    NOTREACHED() << "Lost connection to parent node.";
    return;
  }

  DLOG(INFO) << "Lost connection to peer " << name;
}

void ChildNodeController::OnHelloChildMessage(
    const ports::NodeName& from_node,
    const ports::NodeName& parent_name,
    const ports::NodeName& token_name) {
  if (from_node != ports::kInvalidNodeName) {
    DLOG(ERROR) << "Received invalid HELLO_CHILD from node " << from_node;
    node_->DropPeer(from_node);
    return;
  }

  if (parent_name_ != ports::kInvalidNodeName) {
    NOTREACHED() << "Received duplicate HELLO_CHILD message.";
    return;
  }

  DLOG(INFO) << "Child " << node_->name() << " received hello from parent "
      << parent_name;

  parent_name_ = parent_name;
  bootstrap_channel_->SendMessage(
      NodeChannel::NewHelloParentMessage(token_name, node_->name()));
  node_->AddPeer(parent_name_, std::move(bootstrap_channel_));
}

void ChildNodeController::OnHelloParentMessage(
    const ports::NodeName& from_node,
    const ports::NodeName& token_name,
    const ports::NodeName& child_name) {
  DLOG(ERROR) << "Received invalid HELLO_PARENT message from " << from_node;
  node_->DropPeer(from_node);
}

void ChildNodeController::OnEventMessage(const ports::NodeName& from_node,
                                         ports::Event event) {
  NOTIMPLEMENTED();
}

}  // namespace edk
}  // namespace mojo
