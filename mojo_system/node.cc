// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ports/mojo_system/node.h"

#include "base/bind.h"
#include "base/logging.h"
#include "crypto/random.h"
#include "ports/mojo_system/core.h"
#include "ports/mojo_system/node_controller.h"

namespace mojo {
namespace edk {

namespace {

template <typename T>
void GenerateRandomName(T* out) { crypto::RandBytes(out, sizeof(T)); }

struct PortObserverHolder : public ports::UserData {
  scoped_ptr<Node::PortObserver> observer;

  explicit PortObserverHolder(scoped_ptr<Node::PortObserver> o)
      : observer(std::move(o)) {}
};

}  // namespace

Node::~Node() {}

Node::Node(Core* core)
    : core_(core), event_thread_("EDK ports node event thread") {
  GenerateRandomName(&name_);
  DLOG(INFO) << "Initializing node " << name_;

  node_.reset(new ports::Node(name_, this));
  event_thread_.Start();
}

void Node::ConnectToPeer(const ports::NodeName& peer_name,
                         ScopedPlatformHandle platform_handle) {
  scoped_ptr<NodeChannel> channel(
      new NodeChannel(this, std::move(platform_handle),
                      core()->io_task_runner()));
  channel->SetRemoteNodeName(peer_name);

  DCHECK(controller_);
  controller_->AcceptPeer(peer_name, std::move(channel));
}

bool Node::HasPeer(const ports::NodeName& node) {
  base::AutoLock lock(peers_lock_);
  return peers_.find(node) != peers_.end();
}

void Node::AddPeer(const ports::NodeName& name,
                   scoped_ptr<NodeChannel> channel) {
  DCHECK(name != ports::kInvalidNodeName);
  {
    base::AutoLock lock(peers_lock_);
    channel->SetRemoteNodeName(name);
    auto result = peers_.insert(std::make_pair(name, std::move(channel)));
    // This can happen normally if two nodes race to be introduced to each
    // other. The losing pipe will be silently closed and introduction should
    // not be affected.
    LOG_IF(INFO, !result.second) << "Ignoring duplicate peer name " << name;
  }
}

void Node::DropPeer(const ports::NodeName& name) {
  ports::NodeName peer;

  {
    base::AutoLock lock(peers_lock_);
    auto it = peers_.find(name);
    if (it == peers_.end())
      return;
    peer = it->first;
    peers_.erase(it);
    DLOG(INFO) << "Dropped peer " << peer;
  }

  DCHECK(controller_);
  controller_->OnPeerLost(peer);
  node_->LostConnectionToNode(name);
}

void Node::SendPeerMessage(const ports::NodeName& name,
                           NodeChannel::OutgoingMessagePtr message) {
  {
    base::AutoLock lock(peers_lock_);
    auto it = peers_.find(name);
    if (it != peers_.end()) {
      it->second->SendMessage(std::move(message));
      return;
    }
  }

  controller_->RouteMessageToUnknownPeer(name, std::move(message));
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
                           scoped_ptr<PortObserver> observer) {
  DCHECK(observer);
  std::shared_ptr<ports::UserData> user_data(
      new PortObserverHolder(std::move(observer)));
  node_->SetUserData(port_name, user_data);
}

int Node::SendMessage(const ports::PortName& port_name,
                      ports::ScopedMessage message) {
  return node_->SendMessage(port_name, std::move(message));
}

void Node::ClosePort(const ports::PortName& port_name) {
  int rv = node_->ClosePort(port_name);
  DCHECK_EQ(rv, ports::OK) << "ClosePort failed: " << rv;
}

void Node::GenerateRandomPortName(ports::PortName* port_name) {
  GenerateRandomName(port_name);
}

void Node::SendEvent(const ports::NodeName& node, ports::Event event) {
  if (node == name_) {
    event_thread_.task_runner()->PostTask(
        FROM_HERE,
        base::Bind(&Node::AcceptEventOnEventThread,
                   base::Unretained(this), base::Passed(&event)));
  } else {
    SendPeerMessage(node, NodeChannel::NewEventMessage(std::move(event)));
  }
}

void Node::MessagesAvailable(const ports::PortName& port,
                             std::shared_ptr<ports::UserData> user_data) {
  PortObserverHolder* user_data_holder =
      static_cast<PortObserverHolder*>(user_data.get());
  if (user_data_holder) {
    PortObserver* observer = user_data_holder->observer.get();
    DCHECK(observer);
    observer->OnMessagesAvailable();
  }
}

void Node::OnMessageReceived(const ports::NodeName& from_node,
                             NodeChannel::IncomingMessagePtr message) {
  DCHECK(controller_);

  DLOG(INFO) << "Node " << name_ << " received " << message->type()
             << " message from node " << from_node;

  switch (message->type()) {
    case NodeChannel::MessageType::HELLO_CHILD: {
      const auto& data = message->payload<NodeChannel::HelloChildMessageData>();
      controller_->OnHelloChildMessage(
          from_node, data.parent_name, data.token_name);
      break;
    }

    case NodeChannel::MessageType::HELLO_PARENT: {
      const auto& data =
          message->payload<NodeChannel::HelloParentMessageData>();
      controller_->OnHelloParentMessage(
          from_node, data.token_name, data.child_name);
      break;
    }

    case NodeChannel::MessageType::EVENT: {
      // TODO: Make this less bad
      const auto& data = message->payload<NodeChannel::EventMessageData>();
      ports::Event event(static_cast<ports::Event::Type>(data.type));
      event.port_name = data.port_name;
      if (event.type == ports::Event::kAcceptMessage) {
        size_t message_size = message->payload_size() -
            sizeof(NodeChannel::EventMessageData);
        const ports::Message* m =
            reinterpret_cast<const ports::Message*>(&(&data)[1]);
        ports::Message* own_m = ports::AllocMessage(m->num_bytes, m->num_ports);
        memcpy(own_m, m, message_size);
        own_m->ports = reinterpret_cast<ports::PortDescriptor*>(
            reinterpret_cast<char*>(own_m) + sizeof(ports::Message));
        own_m->bytes = reinterpret_cast<char*>(own_m->ports) +
            own_m->num_ports * sizeof(ports::PortDescriptor);
        event.message.reset(own_m);
      }
      memcpy(&event.observe_proxy, &data.observe_proxy,
          sizeof(event.observe_proxy));

      SendEvent(name_, std::move(event));
      break;
    }

    case NodeChannel::MessageType::CONNECT_PORT: {
      const auto& data =
          message->payload<NodeChannel::ConnectPortMessageData>();
      // TODO: yikes
      std::string token(reinterpret_cast<const char*>(&(&data)[1]),
                        message->payload_size() - sizeof(data));
      controller_->OnConnectPortMessage(from_node, data.child_port_name, token);
      break;
    }

    case NodeChannel::MessageType::CONNECT_PORT_ACK: {
      const auto& data =
          message->payload<NodeChannel::ConnectPortAckMessageData>();
      controller_->OnConnectPortAckMessage(
          from_node, data.child_port_name, data.parent_port_name);
      break;
    }

    case NodeChannel::MessageType::REQUEST_INTRODUCTION: {
      const auto& data =
          message->payload<NodeChannel::IntroductionMessageData>();
      controller_->OnRequestIntroductionMessage(from_node, data.name);
      break;
    }

    case NodeChannel::MessageType::INTRODUCE: {
      const auto& data =
          message->payload<NodeChannel::IntroductionMessageData>();
      ScopedPlatformHandleVectorPtr handles = message->TakeHandles();
      ScopedPlatformHandle handle;
      if (handles && !handles->empty()) {
        handle = ScopedPlatformHandle(handles->at(0));
        handles->clear();
      }
      controller_->OnIntroduceMessage(from_node, data.name, std::move(handle));
      break;
    }

    default:
      OnChannelError(from_node);
      break;
  }
}

void Node::OnChannelError(const ports::NodeName& from_node) {
  DropPeer(from_node);
}

void Node::AcceptEventOnEventThread(ports::Event event) {
  node_->AcceptEvent(std::move(event));
}

}  // namespace edk
}  // namespace mojo
