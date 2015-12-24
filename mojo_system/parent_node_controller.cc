// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ports/mojo_system/parent_node_controller.h"

#include "base/callback.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "crypto/random.h"
#include "mojo/edk/embedder/platform_channel_pair.h"
#include "ports/mojo_system/node.h"
#include "ports/mojo_system/node_channel.h"

namespace mojo {
namespace edk {

ParentNodeController::ReservedPort::ReservedPort() {}

ParentNodeController::ReservedPort::~ReservedPort() {}

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

void ParentNodeController::OnConnectPortMessage(
    const ports::NodeName& from_node,
    const ports::PortName& child_port_name,
    const std::string& token) {
  if (!node_->HasPeer(from_node)) {
    DLOG(ERROR) << "Ignoring CONNECT_PORT message from unknown node "
        << from_node;
    return;
  }

  ports::PortName parent_port_name;
  base::Closure callback;
  {
    base::AutoLock lock(reserved_ports_lock_);
    auto it = reserved_ports_.find(token);
    if (it == reserved_ports_.end()) {
      parent_port_name = ports::kInvalidPortName;
    } else {
      parent_port_name = it->second.local_port;
      callback = it->second.callback;
      reserved_ports_.erase(it);
    }
  }

  if (!callback.is_null()) {
    node_->InitializePort(parent_port_name, from_node, child_port_name);
    callback.Run();
  }

  node_->SendPeerMessage(from_node,
      NodeChannel::NewConnectPortAckMessage(child_port_name, parent_port_name));
}

void ParentNodeController::OnConnectPortAckMessage(
    const ports::NodeName& from_node,
    const ports::PortName& child_port_name,
    const ports::PortName& parent_port_name) {
  DLOG(INFO) << "Ignoring CONNECT_PORT_ACK message in parent.";
  node_->DropPeer(from_node);
}

void ParentNodeController::OnRequestIntroductionMessage(
    const ports::NodeName& from_node,
    const ports::NodeName& name) {
  if (from_node == name || name == ports::kInvalidNodeName) {
    DLOG(ERROR) << "Rejecting invalid REQUEST_INTRODUCTION message.";
    node_->DropPeer(from_node);
    return;
  }

  if (!node_->HasPeer(name)) {
    node_->SendPeerMessage(from_node,
        NodeChannel::NewIntroduceMessage(name, ScopedPlatformHandle()));
  } else {
    PlatformChannelPair new_channel;
    node_->SendPeerMessage(
        from_node,
        NodeChannel::NewIntroduceMessage(name, new_channel.PassServerHandle()));
    node_->SendPeerMessage(
        name,
        NodeChannel::NewIntroduceMessage(from_node,
                                         new_channel.PassClientHandle()));
  }
}

void ParentNodeController::OnIntroduceMessage(
    const ports::NodeName& from_name,
    const ports::NodeName& name,
    ScopedPlatformHandle channel_handle) {
  DLOG(ERROR) << "Ignoring INTRODUCE message in parent.";
  node_->DropPeer(from_name);
}

void ParentNodeController::ReservePortForToken(
    const ports::PortName& port_name,
    const std::string& token,
    const base::Closure& on_connect) {
  ReservedPort reservation;
  reservation.local_port = port_name;
  reservation.callback = on_connect;

  base::AutoLock lock(reserved_ports_lock_);
  auto result = reserved_ports_.insert(std::make_pair(token, reservation));
  if (!result.second)
    DLOG(ERROR) << "Can't reserve port for duplicate token: " << token;
}

void ParentNodeController::ConnectPortByToken(
    const ports::PortName& port_name,
    const std::string& token,
    const base::Closure& on_connect) {
  NOTIMPLEMENTED();
}

void ParentNodeController::RouteMessageToUnknownPeer(
    const ports::NodeName& name,
    NodeChannel::OutgoingMessagePtr message) {
  DLOG(ERROR) << "Dropping message for unknown peer " << name;
}

}  // namespace edk
}  // namespace mojo
