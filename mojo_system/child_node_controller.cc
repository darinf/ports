// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ports/mojo_system/child_node_controller.h"

#include "base/callback.h"
#include "base/logging.h"
#include "ports/include/ports.h"
#include "ports/mojo_system/node.h"

namespace mojo {
namespace edk {

ChildNodeController::PendingTokenConnection::PendingTokenConnection() {}

ChildNodeController::PendingTokenConnection::~PendingTokenConnection() {}

ChildNodeController::ChildNodeController(Node* node) : node_(node) {
  DCHECK(node_);
}

ChildNodeController::~ChildNodeController() {}

bool ChildNodeController::IsConnected() const {
  return parent_name_ != ports::kInvalidNodeName;
}

void ChildNodeController::AcceptPeer(const ports::NodeName& peer_name,
                                     scoped_ptr<NodeChannel> channel) {
  if (!IsConnected()) {
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
  if (!IsConnected() || parent_name_ == name) {
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

  if (IsConnected()) {
    NOTREACHED() << "Received duplicate HELLO_CHILD message.";
    return;
  }

  DLOG(INFO) << "Child " << node_->name() << " received hello from parent "
      << parent_name;

  parent_name_ = parent_name;
  bootstrap_channel_->SendMessage(
      NodeChannel::NewHelloParentMessage(token_name, node_->name()));
  node_->AddPeer(parent_name_, std::move(bootstrap_channel_));

  // Flush any pending token-based port connections.
  std::vector<PendingTokenConnection> connections;
  {
    base::AutoLock lock(pending_token_connections_lock_);
    std::swap(pending_token_connections_, connections);
  }

  for (const PendingTokenConnection& c : connections)
    ConnectPortByTokenNow(c.port, c.token, c.callback);
}

void ChildNodeController::OnHelloParentMessage(
    const ports::NodeName& from_node,
    const ports::NodeName& token_name,
    const ports::NodeName& child_name) {
  DLOG(ERROR) << "Received invalid HELLO_PARENT message from " << from_node;
  node_->DropPeer(from_node);
}

void ChildNodeController::ReservePortForToken(
    const ports::PortName& port_name,
    const std::string& token,
    const base::Closure& on_connect) {
  NOTIMPLEMENTED();
}

void ChildNodeController::ConnectPortByToken(
    const ports::PortName& port_name,
    const std::string& token,
    const base::Closure& on_connect) {
  if (IsConnected()) {
    ConnectPortByTokenNow(port_name, token, on_connect);
  } else {
    PendingTokenConnection pending_connection;
    pending_connection.port = port_name;
    pending_connection.token = token;
    pending_connection.callback = on_connect;

    base::AutoLock lock(pending_token_connections_lock_);
    pending_token_connections_.push_back(pending_connection);
  }
}

void ChildNodeController::ConnectPortByTokenNow(
    const ports::PortName& port_name,
    const std::string& token,
    const base::Closure& on_connect) {
  DCHECK(IsConnected());
  {
    base::AutoLock lock(pending_token_connections_lock_);
    auto result = pending_connection_acks_.insert(
        std::make_pair(port_name, on_connect));
    DCHECK(result.second);
  }
  node_->SendPeerMessage(parent_name_,
      NodeChannel::NewConnectPortMessage(port_name, token));
}

void ChildNodeController::OnConnectPortMessage(
    const ports::NodeName& from_node,
    const ports::PortName& child_port_name,
    const std::string& token) {
  // TODO: maybe it makes sense to allow children to connect to each other
  // using tokens? no interesting use case for it at the moment.
  NOTIMPLEMENTED();
}

void ChildNodeController::OnConnectPortAckMessage(
    const ports::NodeName& from_node,
    const ports::PortName& child_port_name,
    const ports::PortName& parent_port_name) {
  if (from_node != parent_name_) {
    DLOG(ERROR) << "Ignoring CONNECT_PORT_ACK from non-parent node.";
    node_->DropPeer(from_node);
    return;
  }

  base::Closure callback;
  {
    base::AutoLock lock(pending_token_connections_lock_);
    auto it = pending_connection_acks_.find(child_port_name);
    DCHECK(it != pending_connection_acks_.end());
    callback = it->second;
    pending_connection_acks_.erase(it);
  }

  node_->InitializePort(child_port_name, parent_name_, parent_port_name);
  callback.Run();
}

}  // namespace edk
}  // namespace mojo
