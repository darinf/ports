// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_EDK_SYSTEM_NODE_CONTROLLER_H_
#define MOJO_EDK_SYSTEM_NODE_CONTROLLER_H_

#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "base/containers/hash_tables.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/task_runner.h"
#include "mojo/edk/embedder/platform_handle_vector.h"
#include "mojo/edk/embedder/scoped_platform_handle.h"
#include "mojo/edk/system/node_channel.h"
#include "mojo/edk/system/ports/hash_functions.h"
#include "mojo/edk/system/ports/name.h"
#include "mojo/edk/system/ports/node.h"
#include "mojo/edk/system/ports/node_delegate.h"

namespace mojo {
namespace edk {

class Core;
class PortsMessage;

// The owner of ports::Node which facilitates core EDK implementation. All
// public interface methods are safe to call from any thread.
class NodeController : public ports::NodeDelegate,
                       public NodeChannel::Delegate {
 public:
  class PortObserver : public ports::UserData {
   public:
    ~PortObserver() override {}
    virtual void OnPortStatusChanged() = 0;
  };

  // |core| owns and out-lives us.
  explicit NodeController(Core* core);
  ~NodeController() override;

  const ports::NodeName& name() const { return name_; }
  Core* core() const { return core_; }
  ports::Node* node() const { return node_.get(); }

  // Called exactly once, shortly after construction, and before any other
  // methods are called on this object.
  void SetIOTaskRunner(scoped_refptr<base::TaskRunner> io_task_runner);

  // Connects this node to a child node. This node will initiate a handshake.
  void ConnectToChild(ScopedPlatformHandle platform_handle);

  // Connects this node to a parent node. The parent node will initiate a
  // handshake.
  void ConnectToParent(ScopedPlatformHandle platform_handle);

  // Sets a port's observer.
  void SetPortObserver(const ports::PortRef& port,
                       std::shared_ptr<PortObserver> observer);

  scoped_ptr<PortsMessage> AllocMessage(size_t num_payload_bytes,
                                        size_t num_ports);

  // Sends a message on a port to its peer.
  int SendMessage(const ports::PortRef& port_ref,
                  scoped_ptr<PortsMessage> message);

  // Eventually initializes the local port referenced by |port_ref| such that
  // it points to the identified peer port. Initialization is deferred until
  // this node has been acquainted with the peer node.
  void InitializePortDeferred(const ports::PortRef& port_ref,
                              const ports::NodeName& peer_node_name,
                              const ports::PortName& peer_port_name);

 private:
  using NodeMap = std::unordered_map<ports::NodeName, scoped_ptr<NodeChannel>>;
  using OutgoingMessageQueue = std::queue<ports::ScopedMessage>;

  struct DeferredPeerPort {
    ports::PortRef local_port;
    ports::PortName remote_port;
  };

  using DeferredPeerPorts = std::vector<DeferredPeerPort>;

  void ConnectToChildOnIOThread(ScopedPlatformHandle platform_handle);
  void ConnectToParentOnIOThread(ScopedPlatformHandle platform_handle);
  void AddPeer(const ports::NodeName& name,
               scoped_ptr<NodeChannel> channel,
               bool start_channel);
  void DropPeer(const ports::NodeName& name);
  void SendPeerMessage(const ports::NodeName& name,
                       ports::ScopedMessage message);
  void AcceptIncomingMessages();

  // ports::NodeDelegate:
  void GenerateRandomPortName(ports::PortName* port_name) override;
  void AllocMessage(size_t num_header_bytes,
                    size_t num_payload_bytes,
                    size_t num_ports,
                    ports::ScopedMessage* message) override;
  void ForwardMessage(const ports::NodeName& node,
                      ports::ScopedMessage message) override;
  void PortStatusChanged(const ports::PortRef& port) override;

  // NodeChannel::Delegate:
  void OnAcceptChild(const ports::NodeName& from_node,
                     const ports::NodeName& parent_name,
                     const ports::NodeName& token) override;
  void OnAcceptParent(const ports::NodeName& from_node,
                      const ports::NodeName& token,
                      const ports::NodeName& child_name) override;
  void OnPortsMessage(const ports::NodeName& from_node,
                      const void* payload,
                      size_t payload_size,
                      ScopedPlatformHandleVectorPtr platform_handles) override;
  void OnConnectToPort(const ports::NodeName& from_node,
                       const ports::PortName& connector_port_name,
                       const ports::PortName& connectee_port_name) override;
  void OnRequestIntroduction(const ports::NodeName& from_node,
                             const ports::NodeName& name) override;
  void OnIntroduce(const ports::NodeName& from_node,
                   const ports::NodeName& name,
                   ScopedPlatformHandle channel_handle) override;
  void OnChannelError(const ports::NodeName& from_node) override;

  // These are safe to access from any thread as long as the Node is alive.
  Core* const core_;
  const ports::NodeName name_;
  const scoped_ptr<ports::Node> node_;

  scoped_refptr<base::TaskRunner> io_task_runner_;

  // Guards |peers_|, |pending_peer_messages_|, and |pending_peer_ports_|.
  base::Lock peers_lock_;

  // Channels to known peers, including parent and children, if any.
  NodeMap peers_;

  // Outgoing message queues for peers we've heard of but can't yet talk to.
  std::unordered_map<ports::NodeName, OutgoingMessageQueue>
      pending_peer_messages_;

  // Port initializations which have been deferred until a new peer node is
  // discovered.
  std::unordered_map<ports::NodeName, DeferredPeerPorts> pending_peer_ports_;

  // All other fields below must only be accessed on the I/O thread, i.e., the
  // thread on which core_->io_task_runner() runs tasks.

  // The name of our parent node, if any.
  ports::NodeName parent_name_;

  // A channel to our parent during handshake.
  scoped_ptr<NodeChannel> bootstrap_channel_to_parent_;

  // Channels to children during handshake.
  NodeMap pending_children_;

  // Guards |incoming_messages_|.
  base::Lock messages_lock_;
  std::queue<ports::ScopedMessage> incoming_messages_;

  DISALLOW_COPY_AND_ASSIGN(NodeController);
};

}  // namespace edk
}  // namespace mojo

#endif  // MOJO_EDK_SYSTEM_NODE_CONTROLLER_H_
