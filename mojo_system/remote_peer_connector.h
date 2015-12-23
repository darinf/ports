// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PORTS_MOJO_SYSTEM_REMOTE_PEER_CONNECTOR_H_
#define PORTS_MOJO_SYSTEM_REMOTE_PEER_CONNECTOR_H_

#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task_runner.h"
#include "mojo/edk/embedder/scoped_platform_handle.h"
#include "ports/include/ports.h"
#include "ports/mojo_system/channel.h"
#include "ports/mojo_system/message_pipe_dispatcher.h"
#include "ports/mojo_system/node.h"

namespace mojo {
namespace edk {

// This uses arcane magic to establish a message pipe between two endpoints
// connected by an arbitrary platform channel.
//
// Both ends of the channel must be running a Node and must use this connector
// to initialize their corresponding platform handle.
//
// Because this performs its own out-of-band handshake which may complete before
// the nodes have even discovered each other, we need to observe the node to
// watch for new peers.
//
// For convenience this does all its post-construction work on the IO thread.
class RemotePeerConnector : public Channel::Delegate {
 public:
  RemotePeerConnector(Node* node,
                      ScopedPlatformHandle handle,
                      scoped_refptr<base::TaskRunner> io_task_runner,
                      scoped_refptr<MessagePipeDispatcher> dispatcher);

  ~RemotePeerConnector() override;

 private:
  struct MessageData {
    ports::NodeName peer_node_name;
    ports::PortName peer_port_name;
  };

  // Channel::Delegate:
  void OnChannelRead(Channel::IncomingMessage* message) override;
  void OnChannelError() override;

  // Callback run on the IO thread whenever a new peer is added to |node_|.
  void OnPeerAdded(const ports::NodeName& name);

  void ConnectPipeAndGoAway();

  Node* node_;
  scoped_ptr<Node::Observer> node_observer_;

  scoped_refptr<Channel> channel_;
  scoped_refptr<MessagePipeDispatcher> dispatcher_;

  bool peer_connected_ = false;
  bool response_received_ = false;
  ports::NodeName peer_node_name_;
  ports::PortName peer_port_name_;
  base::WeakPtrFactory<RemotePeerConnector> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(RemotePeerConnector);
};

}  // namespace edk
}  // namespace mojo

#endif  // PORTS_MOJO_SYSTEM_REMOTE_PEER_CONNECTOR_H_
