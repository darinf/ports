// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/edk/system/node_controller.h"

#include <algorithm>

#include "base/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/message_loop/message_loop.h"
#include "crypto/random.h"
#include "mojo/edk/embedder/platform_channel_pair.h"
#include "mojo/edk/system/core.h"
#include "mojo/edk/system/ports_message.h"

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

// Used by NodeController to watch for shutdown. Since no IO can happen once
// the IO thread is killed, the NodeController can cleanly drop all its peers
// at that time.
class ThreadDestructionObserver :
    public base::MessageLoop::DestructionObserver {
 public:
  static void Create(scoped_refptr<base::TaskRunner> task_runner,
                     const base::Closure& callback) {
    if (task_runner->RunsTasksOnCurrentThread()) {
      // Owns itself.
      new ThreadDestructionObserver(callback);
    } else {
      task_runner->PostTask(FROM_HERE,
                            base::Bind(&Create, task_runner, callback));
    }
  }

 private:
  explicit ThreadDestructionObserver(const base::Closure& callback)
      : callback_(callback) {
    base::MessageLoop::current()->AddDestructionObserver(this);
  }

  ~ThreadDestructionObserver() override {
    base::MessageLoop::current()->RemoveDestructionObserver(this);
  }

  // base::MessageLoop::DestructionObserver:
  void WillDestroyCurrentMessageLoop() override {
    callback_.Run();
    delete this;
  }

  const base::Closure callback_;

  DISALLOW_COPY_AND_ASSIGN(ThreadDestructionObserver);
};

}  // namespace

NodeController::~NodeController() {}

NodeController::NodeController(Core* core)
    : core_(core),
      name_(GetRandomNodeName()),
      node_(new ports::Node(name_, this)) {
  DVLOG(1) << "Initializing node " << name_;
}

void NodeController::SetIOTaskRunner(
    scoped_refptr<base::TaskRunner> task_runner) {
  io_task_runner_ = task_runner;
  ThreadDestructionObserver::Create(
      io_task_runner_,
      base::Bind(&NodeController::DropAllPeers, base::Unretained(this)));
}

void NodeController::ConnectToChild(base::ProcessHandle process_handle,
                                    ScopedPlatformHandle platform_handle) {
  io_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&NodeController::ConnectToChildOnIOThread,
                 base::Unretained(this),
                 process_handle,
                 base::Passed(&platform_handle)));
}

void NodeController::ConnectToParent(ScopedPlatformHandle platform_handle) {
  io_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&NodeController::ConnectToParentOnIOThread,
                 base::Unretained(this),
                 base::Passed(&platform_handle)));
}

void NodeController::SetPortObserver(
    const ports::PortRef& port,
    const scoped_refptr<PortObserver>& observer) {
  DCHECK(observer);
  node_->SetUserData(port, observer);
}

scoped_ptr<PortsMessage> NodeController::AllocMessage(size_t num_payload_bytes,
                                                      size_t num_ports) {
  ports::ScopedMessage m;
  int rv = node_->AllocMessage(num_payload_bytes, num_ports, &m);
  if (rv != ports::OK)
    return nullptr;
  DCHECK(m);

  return make_scoped_ptr(static_cast<PortsMessage*>(m.release()));
}

int NodeController::SendMessage(const ports::PortRef& port,
                                scoped_ptr<PortsMessage> message) {
  ports::ScopedMessage ports_message(message.release());
  return node_->SendMessage(port, std::move(ports_message));
}

void NodeController::ReservePort(const std::string& token,
                                 ports::PortRef* port_ref) {
  node_->CreateUninitializedPort(port_ref);

  DVLOG(2) << "Reserving port " << port_ref->name() << "@" << name_
           << " for token " << token;

  base::AutoLock lock(reserved_ports_lock_);
  reserved_ports_.insert(std::make_pair(token, *port_ref));
}

void NodeController::ConnectToParentPort(const ports::PortRef& local_port,
                                         const std::string& token) {
  io_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&NodeController::RequestParentPortConnectionOnIOThread,
                 base::Unretained(this), local_port, token));
}

void NodeController::ConnectToChildOnIOThread(
    base::ProcessHandle process_handle,
    ScopedPlatformHandle platform_handle) {
  DCHECK(io_task_runner_->RunsTasksOnCurrentThread());

  scoped_refptr<NodeChannel> channel =
      NodeChannel::Create(this, std::move(platform_handle), io_task_runner_);

  ports::NodeName token;
  GenerateRandomName(&token);

  channel->SetRemoteNodeName(token);
  channel->SetRemoteProcessHandle(process_handle);
  channel->Start();
  channel->AcceptChild(name_, token);

  pending_children_.insert(std::make_pair(token, channel));
}

void NodeController::ConnectToParentOnIOThread(
    ScopedPlatformHandle platform_handle) {
  DCHECK(io_task_runner_->RunsTasksOnCurrentThread());
  DCHECK(parent_name_ == ports::kInvalidNodeName);
  DCHECK(!parent_channel_);

  // At this point we don't know the parent's name, so we can't yet insert it
  // into our |peers_| map. That will happen as soon as we receive an
  // AcceptChild message from them.
  parent_channel_ = NodeChannel::Create(this, std::move(platform_handle),
                                        io_task_runner_);
  parent_channel_->Start();
}

void NodeController::RequestParentPortConnectionOnIOThread(
    const ports::PortRef& local_port,
    const std::string& token) {
  DCHECK(io_task_runner_->RunsTasksOnCurrentThread());

  if (parent_name_ == ports::kInvalidNodeName) {
    PendingPortRequest request;
    request.token = token;
    request.local_port = local_port;
    pending_port_requests_.push_back(request);
    return;
  }

  if (!parent_channel_) {
    DVLOG(1) << "Lost parent node connection.";
    return;
  }

  parent_channel_->RequestPortConnection(local_port.name(), token);
}

scoped_refptr<NodeChannel> NodeController::GetPeerChannel(
    const ports::NodeName& name) {
  base::AutoLock lock(peers_lock_);
  auto it = peers_.find(name);
  if (it == peers_.end())
    return nullptr;
  return it->second;
}

void NodeController::AddPeer(const ports::NodeName& name,
                             scoped_refptr<NodeChannel> channel,
                             bool start_channel) {
  DCHECK(io_task_runner_->RunsTasksOnCurrentThread());

  DCHECK(name != ports::kInvalidNodeName);
  DCHECK(channel);

  channel->SetRemoteNodeName(name);

  base::AutoLock lock(peers_lock_);
  if (peers_.find(name) != peers_.end()) {
    // This can happen normally if two nodes race to be introduced to each
    // other. The losing pipe will be silently closed and introduction should
    // not be affected.
    DVLOG(1) << "Ignoring duplicate peer name " << name;
    return;
  }

  auto result = peers_.insert(std::make_pair(name, channel));
  DCHECK(result.second);

  DVLOG(2) << "Accepting new peer " << name << " on node " << name_;

  if (start_channel)
    channel->Start();

  // Flush any queued message we need to deliver to this node.
  OutgoingMessageQueue pending_messages;
  auto it = pending_peer_messages_.find(name);
  if (it != pending_peer_messages_.end()) {
    auto& message_queue = it->second;
    while (!message_queue.empty()) {
      ports::ScopedMessage message = std::move(message_queue.front());
      channel->PortsMessage(
          static_cast<PortsMessage*>(message.get())->TakeChannelMessage());
      message_queue.pop();
    }
    pending_peer_messages_.erase(it);
  }
}

void NodeController::DropPeer(const ports::NodeName& name) {
  DCHECK(io_task_runner_->RunsTasksOnCurrentThread());

  {
    base::AutoLock lock(peers_lock_);
    auto it = peers_.find(name);

    if (it != peers_.end()) {
      ports::NodeName peer = it->first;
      peers_.erase(it);
      DVLOG(1) << "Dropped peer " << peer;
    }

    pending_peer_messages_.erase(name);
    pending_children_.erase(name);
  }

  node_->LostConnectionToNode(name);
}

void NodeController::SendPeerMessage(const ports::NodeName& name,
                                     ports::ScopedMessage message) {
  PortsMessage* ports_message = static_cast<PortsMessage*>(message.get());

#if defined(OS_WIN)
  if (ports_message->has_handles()) {
    if (!parent_channel_) {
      // Then we are the parent. XXX This is not quite right!
      scoped_refptr<NodeChannel> peer = GetPeerChannel(name);
      if (peer) {
        peer->RelayPortsMessage(
            name, ports_message->TakeChannelMessage());
      } else {
        DLOG(ERROR) << "Oops, unknown child!";
      }
    } else {
      parent_channel_->RelayPortsMessage(
          name, ports_message->TakeChannelMessage());
    }
    return;
  }
#endif

  scoped_refptr<NodeChannel> peer = GetPeerChannel(name);
  if (peer) {
    peer->PortsMessage(ports_message->TakeChannelMessage());
    return;
  }

  if (parent_name_ == ports::kInvalidNodeName) {
    DVLOG(1) << "Dropping message for unknown peer: " << name;
    return;
  }

  if (!parent_channel_) {
    DVLOG(1) << "Lost connection to parent.";
    return;
  }

  // If we don't know who the peer is, queue the message for delivery. If this
  // is the first message queued for the peer, we also ask the parent to
  // introduce us to them.

  bool needs_introduction = false;
  {
    base::AutoLock lock(peers_lock_);
    auto& queue = pending_peer_messages_[name];
    needs_introduction = queue.empty();
    queue.emplace(std::move(message));
  }

  if (needs_introduction)
    parent_channel_->RequestIntroduction(name);
}

void NodeController::AcceptIncomingMessages() {
  std::queue<ports::ScopedMessage> messages;
  {
    base::AutoLock lock(messages_lock_);
    std::swap(messages, incoming_messages_);
  }

  while (!messages.empty()) {
    node_->AcceptMessage(std::move(messages.front()));
    messages.pop();
  }
}

void NodeController::DropAllPeers() {
  if (parent_channel_) {
    // We may not yet have the parent channel held in |peers_|, so we shut it
    // down here just in case. It's safe to call ShutDown() twice on the same
    // NodeChannel.
    parent_channel_->ShutDown();
    parent_channel_ = nullptr;
  }

  std::vector<scoped_refptr<NodeChannel>> all_peers;
  {
    base::AutoLock lock(peers_lock_);
    for (const auto& peer : peers_)
      all_peers.push_back(peer.second);
    for (const auto& peer : pending_children_)
      all_peers.push_back(peer.second);
    peers_.clear();
    pending_children_.clear();
    pending_peer_messages_.clear();
  }

  for (const auto& peer : all_peers)
    peer->ShutDown();
}

void NodeController::GenerateRandomPortName(ports::PortName* port_name) {
  GenerateRandomName(port_name);
}

void NodeController::AllocMessage(size_t num_header_bytes,
                                  size_t num_payload_bytes,
                                  size_t num_ports_bytes,
                                  ports::ScopedMessage* message) {
  message->reset(new PortsMessage(num_header_bytes, num_payload_bytes,
                                  num_ports_bytes, nullptr));
}

void NodeController::ForwardMessage(const ports::NodeName& node,
                                    ports::ScopedMessage message) {
  if (node == name_) {
    // NOTE: It isn't critical that we accept messages on the IO thread.
    // Rather, we just need to avoid re-entering the Node instance within
    // ForwardMessage.

    bool queue_was_empty = false;
    {
      base::AutoLock lock(messages_lock_);
      queue_was_empty = incoming_messages_.empty();
      incoming_messages_.emplace(std::move(message));
    }

    if (queue_was_empty) {
      io_task_runner_->PostTask(
          FROM_HERE,
          base::Bind(&NodeController::AcceptIncomingMessages,
                     base::Unretained(this)));
    }
  } else {
    SendPeerMessage(node, std::move(message));
  }
}

void NodeController::PortStatusChanged(const ports::PortRef& port) {
  scoped_refptr<ports::UserData> user_data;
  node_->GetUserData(port, &user_data);

  PortObserver* observer = static_cast<PortObserver*>(user_data.get());
  if (observer) {
    observer->OnPortStatusChanged();
  } else {
    DVLOG(2) << "Ignoring status change for " << port.name() << " because it "
             << "doesn't have an observer.";
  }
}

void NodeController::OnAcceptChild(const ports::NodeName& from_node,
                                   const ports::NodeName& parent_name,
                                   const ports::NodeName& token) {
  DCHECK(io_task_runner_->RunsTasksOnCurrentThread());

  if (!parent_channel_ || parent_name_ != ports::kInvalidNodeName) {
    DLOG(ERROR) << "Unexpected AcceptChild message from " << from_node;
    DropPeer(from_node);
    return;
  }

  parent_name_ = parent_name;
  parent_channel_->AcceptParent(token, name_);

  for (const auto& request : pending_port_requests_) {
    parent_channel_->RequestPortConnection(request.local_port.name(),
                                           request.token);
  }
  pending_port_requests_.clear();

  DVLOG(1) << "Child " << name_ << " accepting parent " << parent_name;

  AddPeer(parent_name_, parent_channel_, false /* start_channel */);
}

void NodeController::OnAcceptParent(const ports::NodeName& from_node,
                                    const ports::NodeName& token,
                                    const ports::NodeName& child_name) {
  DCHECK(io_task_runner_->RunsTasksOnCurrentThread());

  auto it = pending_children_.find(from_node);
  if (it == pending_children_.end() || token != from_node) {
    DLOG(ERROR) << "Received unexpected AcceptParent message from "
                << from_node;
    DropPeer(from_node);
    return;
  }

  scoped_refptr<NodeChannel> channel = it->second;
  pending_children_.erase(it);

  DCHECK(channel);

  DVLOG(1) << "Parent " << name_ << " accepted child " << child_name;

  AddPeer(child_name, channel, false /* start_channel */);
}

void NodeController::OnPortsMessage(Channel::MessagePtr channel_message) {
  DCHECK(io_task_runner_->RunsTasksOnCurrentThread());

  void* data;
  size_t num_data_bytes;
  NodeChannel::GetPortsMessageData(
      channel_message.get(), &data, &num_data_bytes);

  size_t num_header_bytes, num_payload_bytes, num_ports_bytes;
  ports::Message::Parse(data,
                        num_data_bytes,
                        &num_header_bytes,
                        &num_payload_bytes,
                        &num_ports_bytes);

  ports::ScopedMessage message(
      new PortsMessage(num_header_bytes,
                       num_payload_bytes,
                       num_ports_bytes,
                       std::move(channel_message)));

  node_->AcceptMessage(std::move(message));
}

void NodeController::OnRequestPortConnection(
    const ports::NodeName& from_node,
    const ports::PortName& connector_port_name,
    const std::string& token) {
  DCHECK(io_task_runner_->RunsTasksOnCurrentThread());

  DVLOG(2) << "Node " << name_ << " received RequestPortConnection for token "
           << token << " and port " << connector_port_name << "@" << from_node;

  ports::PortRef local_port;
  {
    base::AutoLock lock(reserved_ports_lock_);
    auto it = reserved_ports_.find(token);
    if (it == reserved_ports_.end()) {
      DVLOG(1) << "Ignoring request to connect to port for unknown token "
               << token;
      return;
    }
    local_port = it->second;
    reserved_ports_.erase(it);
  }

  scoped_refptr<NodeChannel> peer = GetPeerChannel(from_node);
  if (!peer) {
    DVLOG(1) << "Ignoring request to connect to port from unknown node "
             << from_node;
    return;
  }

  // Note: We send our ConnectToPort message before initializing our own end
  // of the port pair to ensure that it arrives  (and thus the remote port is
  // fully initialized) before any messages are sent from our local port.
  peer->ConnectToPort(local_port.name(), connector_port_name);

  // This reserved port should not have been initialized yet.
  //
  // Note that we must not be holding |peers_lock_| when initializing a port,
  // because it may re-enter NodeController to deliver outgoing messages on
  // the port.
  CHECK_EQ(ports::OK, node_->InitializePort(local_port, from_node,
                                            connector_port_name));
}

void NodeController::OnConnectToPort(
    const ports::NodeName& from_node,
    const ports::PortName& connector_port_name,
    const ports::PortName& connectee_port_name) {
  DCHECK(io_task_runner_->RunsTasksOnCurrentThread());

  DVLOG(2) << "Node " << name_ << " received ConnectToPort for local port "
           << connectee_port_name << " to port " << connector_port_name << "@"
           << from_node;

  ports::PortRef connectee_port;
  int rv = node_->GetPort(connectee_port_name, &connectee_port);
  if (rv != ports::OK) {
    DLOG(ERROR) << "Ignoring ConnectToPort for unknown port "
                << connectee_port_name;
    return;
  }

  // It's OK if this port has already been initialized. This message is only
  // sent by the remote peer to ensure the port is ready before it starts
  // us sending messages to it.
  ports::PortStatus port_status;
  rv = node_->GetStatus(connectee_port, &port_status);
  if (rv == ports::OK) {
    DVLOG(1) << "Ignoring ConnectToPort for already-initialized port "
             << connectee_port_name;
    return;
  }

  CHECK_EQ(ports::OK, node_->InitializePort(connectee_port, from_node,
                                            connector_port_name));
}

void NodeController::OnRequestIntroduction(const ports::NodeName& from_node,
                                           const ports::NodeName& name) {
  DCHECK(io_task_runner_->RunsTasksOnCurrentThread());

  scoped_refptr<NodeChannel> requestor = GetPeerChannel(from_node);
  if (from_node == name || name == ports::kInvalidNodeName || !requestor) {
    DLOG(ERROR) << "Rejecting invalid OnRequestIntroduction message from "
                << from_node;
    DropPeer(from_node);
    return;
  }

  scoped_refptr<NodeChannel> new_friend = GetPeerChannel(name);
  if (!new_friend) {
    // We don't know who they're talking about!
    requestor->Introduce(name, ScopedPlatformHandle());
  } else {
    PlatformChannelPair new_channel;
    requestor->Introduce(name, new_channel.PassServerHandle());
    new_friend->Introduce(from_node, new_channel.PassClientHandle());
  }
}

void NodeController::OnIntroduce(const ports::NodeName& from_node,
                                 const ports::NodeName& name,
                                 ScopedPlatformHandle channel_handle) {
  DCHECK(io_task_runner_->RunsTasksOnCurrentThread());

  if (from_node != parent_name_) {
    DLOG(ERROR) << "Received unexpected Introduce message from node "
                << from_node;
    DropPeer(from_node);
    return;
  }

  if (!channel_handle.is_valid()) {
    DLOG(ERROR) << "Could not be introduced to peer " << name;
    base::AutoLock lock(peers_lock_);
    pending_peer_messages_.erase(name);
    return;
  }

  scoped_refptr<NodeChannel> channel =
      NodeChannel::Create(this, std::move(channel_handle), io_task_runner_);

  DVLOG(1) << "Adding new peer " << name << " via parent introduction.";
  AddPeer(name, channel, true /* start_channel */);
}

#if defined(OS_WIN)
void NodeController::OnRelayPortsMessage(const ports::NodeName& destination,
                                         Channel::MessagePtr message) {
  if (destination == name_) {
    // Great, we can deliver this message locally.
    OnPortsMessage(std::move(message));
    return;
  }

  scoped_refptr<NodeChannel> peer = GetPeerChannel(destination);
  if (peer) {
    peer->RelayPortsMessage(destination, std::move(message));
  } else {
    DLOG(ERROR) << "Dropping relay message to unknown node: " << destination;
  }
}
#endif

void NodeController::OnChannelError(const ports::NodeName& from_node) {
  if (io_task_runner_->RunsTasksOnCurrentThread()) {
    DropPeer(from_node);
  } else {
    io_task_runner_->PostTask(
        FROM_HERE,
        base::Bind(&NodeController::DropPeer, base::Unretained(this),
                   from_node));
  }
}

}  // namespace edk
}  // namespace mojo
