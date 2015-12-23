// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ports/mojo_system/remote_peer_connector.h"

#include "base/bind.h"
#include "base/callback.h"
#include "base/memory/ref_counted.h"
#include "base/task_runner.h"
#include "ports/mojo_system/node.h"

namespace mojo {
namespace edk {

namespace {

// A Node::Observer which calls a callback on an IO task runner whenever
// a new peer is added to the Node under observation.
class IOCallbackNodeObserver : public Node::Observer {
 public:
  using OnPeerAddedCallback =
      const base::Callback<void(const ports::NodeName&)>;

  IOCallbackNodeObserver(const OnPeerAddedCallback& callback,
                         scoped_refptr<base::TaskRunner> io_runner)
      : callback_(base::Bind(&IOCallbackNodeObserver::RunCallbackOnTaskRunner,
                             callback, io_runner)) {}

  ~IOCallbackNodeObserver() override {}

 private:
  static void RunCallbackOnTaskRunner(
      const OnPeerAddedCallback& callback,
      scoped_refptr<base::TaskRunner> task_runner,
      const ports::NodeName& name) {
    task_runner->PostTask(FROM_HERE, base::Bind(callback, name));
  }

  // Node::Observer:
  void OnPeerAdded(const ports::NodeName& name) override {
    callback_.Run(name);
  }

  const OnPeerAddedCallback callback_;

  DISALLOW_COPY_AND_ASSIGN(IOCallbackNodeObserver);
};

}  // namespace

RemotePeerConnector::RemotePeerConnector(
    Node* node,
    ScopedPlatformHandle handle,
    scoped_refptr<base::TaskRunner> io_task_runner,
    scoped_refptr<MessagePipeDispatcher> dispatcher)
    : node_(node),
      channel_(Channel::Create(this, std::move(handle), io_task_runner)),
      dispatcher_(dispatcher),
      weak_factory_(this) {
  node_observer_.reset(new IOCallbackNodeObserver(
      base::Bind(&RemotePeerConnector::OnPeerAdded, weak_factory_.GetWeakPtr()),
      io_task_runner));
  node_->AddObserver(node_observer_.get());

  MessageData data;
  data.peer_node_name = node_->name();
  data.peer_port_name = dispatcher_->GetPortName();

  channel_->Start();
  channel_->Write(Channel::OutgoingMessagePtr(
      new Channel::OutgoingMessage(&data, sizeof(data), nullptr)));
}

RemotePeerConnector::~RemotePeerConnector() {
  channel_->ShutDown();
  node_->RemoveObserver(node_observer_.get());
}

void RemotePeerConnector::OnChannelRead(Channel::IncomingMessage* message) {
  // We only accept a single message describing the remote peer.
  response_received_ = true;
  if (message->payload_size() == sizeof(MessageData) &&
      message->num_handles() == 0) {
    const MessageData* data =
        static_cast<const MessageData*>(message->payload());
    peer_node_name_ = data->peer_node_name;
    peer_port_name_ = data->peer_port_name;

    // The peer might not be connected to our node yet, in which case we should
    // be able to hook things up once it's added.
    if (node_->HasPeer(peer_node_name_))
      ConnectPipeAndGoAway();
  } else {
    DLOG(ERROR) << "Remote peer handshake failed. Invalid message received.";
    delete this;
  }
}

void RemotePeerConnector::OnChannelError() {
  // Fail if the channel closed before we got a name.
  if (peer_node_name_ == ports::kInvalidNodeName) {
    DLOG(ERROR) << "Remote peer handshake failed.";
    delete this;
  }
};

void RemotePeerConnector::OnPeerAdded(const ports::NodeName& name) {
  if (peer_node_name_ != ports::kInvalidNodeName && peer_node_name_ == name)
    ConnectPipeAndGoAway();
}

void RemotePeerConnector::ConnectPipeAndGoAway() {
  DCHECK(peer_node_name_ != ports::kInvalidNodeName);
  DCHECK(peer_port_name_ != ports::kInvalidPortName);

  dispatcher_->SetRemotePeer(peer_node_name_, peer_port_name_);
  delete this;
}

}  // namespace edk
}  // namespace mojo
