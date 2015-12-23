// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ports/mojo_system/parent_node_controller.h"

#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "crypto/random.h"
#include "ports/mojo_system/node.h"
#include "ports/mojo_system/node_channel.h"

namespace mojo {
namespace edk {

ParentNodeController::ParentNodeController(Node* node) : node_(node) {
  DCHECK(node_);
}

ParentNodeController::~ParentNodeController() {}

void ParentNodeController::AcceptPeer(const ports::NodeName& peer_name,
                                      scoped_ptr<NodeChannel> channel) {
  // We should only receive AcceptPeer requests from new child nodes whose
  // names are always unkown.
  DCHECK(peer_name == ports::kInvalidNodeName);

  ports::NodeName token_name;
  crypto::RandBytes(&token_name, sizeof(ports::NodeName));

  // Use the token as a temporary name so we can keep track of requests coming
  // from this channel.
  channel->SetRemoteNodeName(token_name);
  channel->Start();
  channel->SendMessage(
      NodeChannel::NewHelloChildMessage(node_->name(), token_name));

  base::AutoLock lock(pending_handshakes_lock_);
  pending_handshakes_.insert(std::make_pair(token_name, std::move(channel)));
}

void ParentNodeController::OnPeerLost(const ports::NodeName& name) {
  DLOG(INFO) << "Lost connection from " << name;

  // Make sure we erase the channel if this was from a pending handshake.
  base::AutoLock lock(pending_handshakes_lock_);
  pending_handshakes_.erase(name);
}

void ParentNodeController::OnHelloChildMessage(
    const ports::NodeName& from_node,
    const ports::NodeName& parent_name,
    const ports::NodeName& token_name) {
  DLOG(ERROR) << "Received unexpected HELLO_CHILD message.";
  {
    base::AutoLock lock(pending_handshakes_lock_);
    if (pending_handshakes_.erase(from_node))
      return;
  }
  node_->DropPeer(from_node);
}

void ParentNodeController::OnHelloParentMessage(
    const ports::NodeName& from_node,
    const ports::NodeName& token_name,
    const ports::NodeName& child_name) {
  scoped_ptr<NodeChannel> channel;
  {
    base::AutoLock lock(pending_handshakes_lock_);
    auto it = pending_handshakes_.find(from_node);
    if (it == pending_handshakes_.end()) {
      DLOG(ERROR) << "Received unexpected HELLO_PARENT message.";
      node_->DropPeer(from_node);
      return;
    }
    channel = std::move(it->second);
    pending_handshakes_.erase(it);
  }

  if (token_name != from_node) {
    DLOG(ERROR) << "Handshake token mismatch from child " << child_name;
    return;
  }

  DCHECK(channel);
  node_->AddPeer(child_name, std::move(channel));

  DLOG(INFO) << "Parent accepted handshake from child " << child_name;
}

void ParentNodeController::OnEventMessage(const ports::NodeName& from_node,
                                          ports::Event event) {
  NOTIMPLEMENTED();
}

}  // namespace edk
}  // namespace mojo
