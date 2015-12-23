// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ports/mojo_system/node.h"

#include "base/bind.h"
#include "base/logging.h"
#include "crypto/random.h"
#include "ports/mojo_system/node_controller.h"

namespace mojo {
namespace edk {

namespace {

template <typename T>
void GenerateRandomName(T* out) { crypto::RandBytes(out, sizeof(T)); }

struct PortObserverHolder : public ports::UserData {
  Node::PortObserver* observer;

  explicit PortObserverHolder(Node::PortObserver* o) : observer(o) {}
};

}  // namespace

Node::~Node() {}

Node::Node() : event_thread_("EDK ports node event thread") {
  GenerateRandomName(&name_);
  DLOG(INFO) << "Initializing node " << name_;

  node_.reset(new ports::Node(name_, this));
  event_thread_.Start();
}

void Node::ConnectToPeer(
    const ports::NodeName& peer_name,
    ScopedPlatformHandle platform_handle,
    const scoped_refptr<base::TaskRunner>& io_task_runner) {
  scoped_ptr<NodeChannel> channel(
      new NodeChannel(this, std::move(platform_handle), io_task_runner));
  channel->SetRemoteNodeName(peer_name);

  DCHECK(controller_);
  controller_->AcceptPeer(peer_name, std::move(channel));
}

void Node::AddPeer(const ports::NodeName& name,
                   scoped_ptr<NodeChannel> channel) {
  DCHECK(name != ports::kInvalidNodeName);
  base::AutoLock lock(peers_lock_);
  channel->SetRemoteNodeName(name);
  auto result = peers_.insert(std::make_pair(name, std::move(channel)));
  DLOG_IF(ERROR, !result.second) << "Ignoring duplicate peer name " << name;
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
}

void Node::CreatePortPair(ports::PortName* port0, ports::PortName* port1) {
  int rv = node_->CreatePortPair(port0, port1);
  DCHECK_EQ(rv, ports::OK);
}

void Node::SetPortObserver(const ports::PortName& port_name,
                           PortObserver* observer) {
  if (observer == nullptr) {
    node_->SetUserData(port_name, std::shared_ptr<ports::UserData>());
  } else {
    std::shared_ptr<ports::UserData> user_data(
        new PortObserverHolder(observer));
    node_->SetUserData(port_name, std::move(user_data));
  }
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
    base::AutoLock lock(peers_lock_);
    auto it = peers_.find(node);
    if (it == peers_.end()) {
      DLOG(ERROR) << "Cannot dispatch event to unknown peer " << node;
      return;
    }
    // TODO: remote dispatch
    NOTIMPLEMENTED();
  }
}

void Node::MessagesAvailable(const ports::PortName& port,
                             std::shared_ptr<ports::UserData> user_data) {
  int rv;
  do {
    ports::ScopedMessage message;
    rv = node_->GetMessage(port, &message);
    if (rv == ports::OK && !message)
      return;

    PortObserver* observer =
        static_cast<PortObserverHolder*>(user_data.get())->observer;
    DCHECK(observer) << "Received a message on a port with no observer.";

    if (rv == ports::OK) {
      observer->OnMessageAvailable(port, std::move(message));
    } else {
      observer->OnPeerClosed(port);
    }
  } while (rv == ports::OK);
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
      controller_->OnEventMessage(from_node, std::move(event));
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
