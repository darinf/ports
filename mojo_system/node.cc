// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ports/mojo_system/node.h"

#include "base/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "crypto/random.h"
#include "mojo/edk/embedder/platform_channel_pair.h"
#include "ports/mojo_system/core.h"

namespace mojo {
namespace edk {

namespace {

template <typename T>
void GenerateRandomName(T* out) { crypto::RandBytes(out, sizeof(T)); }

ports::NodeName GetRandomNodeName() {
  ports::NodeName name;
  GenerateRandomName(&name);
  return name;
}

}  // namespace

Node::PendingTokenConnection::PendingTokenConnection() {}

Node::PendingTokenConnection::~PendingTokenConnection() {}

Node::ReservedPort::ReservedPort() {}

Node::ReservedPort::~ReservedPort() {}

Node::~Node() {}

Node::Node(Core* core)
    : core_(core),
      name_(GetRandomNodeName()),
      node_(new ports::Node(name_, this)) {
  DLOG(INFO) << "Initializing node " << name_;
}

void Node::ConnectToChild(ScopedPlatformHandle platform_handle) {
  scoped_ptr<NodeChannel> channel(
      new NodeChannel(this, std::move(platform_handle),
                      core_->io_task_runner()));

  ports::NodeName token;
  GenerateRandomName(&token);

  base::AutoLock lock(lock_);

  channel->SetRemoteNodeName(token);
  channel->Start();
  channel->AcceptChild(name_, token);

  pending_children_.insert(std::make_pair(token, std::move(channel)));
}

void Node::ConnectToParent(ScopedPlatformHandle platform_handle) {
  base::AutoLock lock(lock_);
  DCHECK(parent_name_ == ports::kInvalidNodeName);
  DCHECK(!bootstrap_channel_to_parent_);

  scoped_ptr<NodeChannel> channel(
      new NodeChannel(this, std::move(platform_handle),
                      core_->io_task_runner()));
  bootstrap_channel_to_parent_ = std::move(channel);
  bootstrap_channel_to_parent_->Start();
}

void Node::AddPeer(const ports::NodeName& name,
                   scoped_ptr<NodeChannel> channel,
                   bool start_channel) {
  base::AutoLock lock(lock_);
  AddPeerNoLock(name, std::move(channel), start_channel);
}

void Node::DropPeer(const ports::NodeName& name) {
  base::AutoLock lock(lock_);
  DropPeerNoLock(name);
}

void Node::SendPeerEvent(const ports::NodeName& name, ports::Event event) {
  base::AutoLock lock(lock_);
  auto it = peers_.find(name);
  if (it != peers_.end()) {
    it->second->Event(std::move(event));
    return;
  }

  if (parent_name_ == ports::kInvalidNodeName) {
    DLOG(INFO) << "Dropping event for unknown peer: " << name;
    return;
  }

  // If we don't know who the peer is, queue the event for delivery and ask our
  // parent node to introduce us.
  auto& queue = pending_peer_events_[name];
  if (queue.empty()) {
    auto it = peers_.find(parent_name_);
    DCHECK(it != peers_.end());
    it->second->RequestIntroduction(name);
  }
  queue.emplace(std::move(event));
}

void Node::CreateUninitializedPort(ports::PortName* port_name) {
  node_->CreatePort(port_name);
}

void Node::InitializePort(const ports::PortName& port_name,
                          const ports::NodeName& peer_node_name,
                          const ports::PortName& peer_port_name) {
  int rv = node_->InitializePort(port_name, peer_node_name, peer_port_name);
  DCHECK_EQ(rv, ports::OK);
}

void Node::CreatePortPair(ports::PortName* port0, ports::PortName* port1) {
  int rv = node_->CreatePortPair(port0, port1);
  DCHECK_EQ(rv, ports::OK);
}

void Node::SetPortObserver(const ports::PortName& port_name,
                           std::shared_ptr<PortObserver> observer) {
  DCHECK(observer);
  node_->SetUserData(port_name, std::move(observer));
}

int Node::SendMessage(const ports::PortName& port_name,
                      ports::ScopedMessage message) {
  return node_->SendMessage(port_name, std::move(message));
}

void Node::ClosePort(const ports::PortName& port_name) {
  int rv = node_->ClosePort(port_name);
  DCHECK_EQ(rv, ports::OK) << "ClosePort failed: " << rv;
}

void Node::ReservePortForToken(const ports::PortName& port_name,
                               const std::string& token,
                               const base::Closure& on_connect) {
  ReservedPort reservation;
  reservation.local_port = port_name;
  reservation.callback = on_connect;

  base::AutoLock lock(lock_);
  auto result = reserved_ports_.insert(std::make_pair(token, reservation));
  if (!result.second)
    DLOG(ERROR) << "Can't reserve port for duplicate token: " << token;
}

void Node::ConnectToParentPortByToken(const std::string& token,
                                      const ports::PortName& local_port,
                                      const base::Closure& on_connect) {
  base::AutoLock lock(lock_);
  if (parent_name_ != ports::kInvalidNodeName) {
    ConnectToParentPortByTokenNowNoLock(token, local_port, on_connect);
  } else {
    PendingTokenConnection pending_connection;
    pending_connection.port = local_port;
    pending_connection.token = token;
    pending_connection.callback = on_connect;

    pending_token_connections_.push_back(pending_connection);
  }
}

void Node::AddPeerNoLock(const ports::NodeName& name,
                         scoped_ptr<NodeChannel> channel,
                         bool start_channel) {
  DCHECK(name != ports::kInvalidNodeName);
  DCHECK(channel);

  channel->SetRemoteNodeName(name);

  if (peers_.find(name) != peers_.end()) {
    // This can happen normally if two nodes race to be introduced to each
    // other. The losing pipe will be silently closed and introduction should
    // not be affected.
    LOG(INFO) << "Ignoring duplicate peer name " << name;
    return;
  }

  DLOG(INFO) << "Accepting new peer " << name << " on node " << name_;

  if (start_channel)
    channel->Start();

  OutgoingEventQueue pending_events;
  auto it = pending_peer_events_.find(name);
  if (it != pending_peer_events_.end()) {
    auto& event_queue = it->second;
    while (!event_queue.empty()) {
      channel->Event(std::move(event_queue.front()));
      event_queue.pop();
    }
    pending_peer_events_.erase(it);
  }

  auto result = peers_.insert(std::make_pair(name, std::move(channel)));
  DCHECK(result.second);
}

void Node::DropPeerNoLock(const ports::NodeName& name) {
  auto it = peers_.find(name);
  if (it != peers_.end()) {
    ports::NodeName peer = it->first;
    peers_.erase(it);
    DLOG(INFO) << "Dropped peer " << peer;
  }

  pending_peer_events_.erase(name);
  pending_children_.erase(name);

  {
    base::AutoUnlock unlock(lock_);
    node_->LostConnectionToNode(name);
  }
}

void Node::ConnectToParentPortByTokenNowNoLock(
    const std::string& token,
    const ports::PortName& local_port,
    const base::Closure& on_connect) {
  DCHECK(parent_name_ != ports::kInvalidNodeName);

  auto result = pending_connection_acks_.insert(
      std::make_pair(local_port, on_connect));
  DCHECK(result.second);

  auto it = peers_.find(parent_name_);
  it->second->ConnectToPort(token, local_port);
}

void Node::AcceptEventOnIOThread(ports::Event event) {
  DCHECK(core_->io_task_runner()->RunsTasksOnCurrentThread());
  node_->AcceptEvent(std::move(event));
}

void Node::GenerateRandomPortName(ports::PortName* port_name) {
  GenerateRandomName(port_name);
}

void Node::SendEvent(const ports::NodeName& node, ports::Event event) {
  if (node == name_) {
    // NOTE: It isn't critical that we accept the event on the IO thread.
    // Rather, we just need to avoid re-entering the Node instance.
    core_->io_task_runner()->PostTask(
        FROM_HERE,
        base::Bind(&Node::AcceptEventOnIOThread,
                   base::Unretained(this),
                   base::Passed(&event)));
  } else {
    SendPeerEvent(node, std::move(event));
  }
}

void Node::MessagesAvailable(const ports::PortName& port,
                             std::shared_ptr<ports::UserData> user_data) {
  PortObserver* observer = static_cast<PortObserver*>(user_data.get());
  if (observer)
    observer->OnMessagesAvailable();
}

void Node::OnAcceptChild(const ports::NodeName& from_node,
                         const ports::NodeName& parent_name,
                         const ports::NodeName& token) {
  base::AutoLock lock(lock_);

  if (!bootstrap_channel_to_parent_) {
    DLOG(ERROR) << "Unexpected AcceptChild message from " << from_node;
    DropPeerNoLock(from_node);
    return;
  }

  parent_name_ = parent_name;
  bootstrap_channel_to_parent_->AcceptParent(token, name_);
  AddPeerNoLock(parent_name_, std::move(bootstrap_channel_to_parent_),
                false /* start_channel */);

  // Flush any pending token-based port connections.
  for (const PendingTokenConnection& c : pending_token_connections_)
    ConnectToParentPortByTokenNowNoLock(c.token, c.port, c.callback);

  DLOG(INFO) << "Child " << name_ << " accepted parent " << parent_name;
}

void Node::OnAcceptParent(const ports::NodeName& from_node,
                          const ports::NodeName& token,
                          const ports::NodeName& child_name) {
  base::AutoLock lock(lock_);
  scoped_ptr<NodeChannel> channel;

  auto it = pending_children_.find(from_node);
  if (it == pending_children_.end() || token != from_node) {
    DLOG(ERROR) << "Received unexpected AcceptParent message from "
                << from_node;
    DropPeerNoLock(from_node);
    return;
  }

  channel = std::move(it->second);
  pending_children_.erase(it);

  DCHECK(channel);

  AddPeerNoLock(child_name, std::move(channel), false /* start_channel */);
}

void Node::OnEvent(const ports::NodeName& from_node,
                   ports::Event event) {
  AcceptEventOnIOThread(std::move(event));
}

void Node::OnConnectToPort(const ports::NodeName& from_node,
                           const ports::PortName& connector_port,
                           const std::string& token) {
  base::AutoLock lock(lock_);

  auto port_it = reserved_ports_.find(token);
  auto peer_it = peers_.find(from_node);
  if (port_it == reserved_ports_.end() || peer_it == peers_.end()) {
    DLOG(ERROR) << "Ignoring invalid ConnectToPort from node " << from_node
                << " for token " << token;
    DropPeerNoLock(from_node);
    return;
  }

  ports::PortName parent_port_name = port_it->second.local_port;
  base::Closure callback = port_it->second.callback;
  reserved_ports_.erase(port_it);

  DCHECK(!callback.is_null());

  InitializePort(parent_port_name, from_node, connector_port);
  callback.Run();

  peer_it->second->ConnectToPortAck(connector_port, parent_port_name);
}

void Node::OnConnectToPortAck(const ports::NodeName& from_node,
                              const ports::PortName& connector_port,
                              const ports::PortName& connectee_port) {
  base::AutoLock lock(lock_);

  if (from_node != parent_name_) {
    DLOG(ERROR) << "Ignoring ConnectToPortAck from non-parent node "
                << from_node;
    DropPeerNoLock(from_node);
    return;
  }

  auto it = pending_connection_acks_.find(connector_port);
  DCHECK(it != pending_connection_acks_.end());
  base::Closure callback = it->second;
  pending_connection_acks_.erase(it);

  DCHECK(!callback.is_null());

  InitializePort(connector_port, parent_name_, connectee_port);
  callback.Run();
}

void Node::OnRequestIntroduction(const ports::NodeName& from_node,
                                 const ports::NodeName& name) {
  if (from_node == name || name == ports::kInvalidNodeName) {
    DLOG(ERROR) << "Rejecting invalid OnRequestIntroduction message from "
                << from_node;
    DropPeer(from_node);
    return;
  }

  base::AutoLock lock(lock_);
  auto it = peers_.find(from_node);
  DCHECK(it != peers_.end());

  NodeChannel* requestor = it->second.get();
  it = peers_.find(name);
  if (it == peers_.end()) {
    requestor->Introduce(name, ScopedPlatformHandle());
  } else {
    PlatformChannelPair new_channel;
    requestor->Introduce(name, new_channel.PassServerHandle());
    it->second->Introduce(from_node, new_channel.PassClientHandle());
  }
}

void Node::OnIntroduce(const ports::NodeName& from_node,
                       const ports::NodeName& name,
                       ScopedPlatformHandle channel_handle) {
  base::AutoLock lock(lock_);

  if (from_node != parent_name_) {
    DLOG(INFO) << "Received unexpected Introduce message from node "
               << from_node;
    DropPeerNoLock(from_node);
    return;
  }

  if (!channel_handle.is_valid()) {
    DLOG(ERROR) << "Could not be introduced to peer " << name;
    pending_peer_events_.erase(name);
    return;
  }

  scoped_ptr<NodeChannel> channel(
      new NodeChannel(this, std::move(channel_handle),
                      core_->io_task_runner()));
  AddPeerNoLock(name, std::move(channel), true /* start_channel */);
}

void Node::OnChannelError(const ports::NodeName& from_node) {
  DropPeer(from_node);
}

}  // namespace edk
}  // namespace mojo
