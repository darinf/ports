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

  // Reserves a port associated with |token|. A peer may associate one of their
  // own ports with this one by sending us a RequestPortConnection message with
  // the same token value.
  //
  // Note that the reservation is made synchronously. In order to avoid races,
  // reservations should be acquired before |token| is communicated to any
  // potential peer.
  void ReservePort(const std::string& token, ports::PortRef* port_ref);

  // Eventually initializes a local port with a parent port peer identified by
  // |token|. The parent should also have |token| and should alrady have
  // reserved a port for it.
  void ConnectToParentPort(const ports::PortRef& local_port,
                           const std::string& token);

 private:
  using NodeMap = std::unordered_map<ports::NodeName,
                                     scoped_refptr<NodeChannel>>;
  using OutgoingMessageQueue = std::queue<ports::ScopedMessage>;

  struct PendingPortRequest {
    std::string token;
    ports::PortRef local_port;
  };

  void ConnectToChildOnIOThread(ScopedPlatformHandle platform_handle);
  void ConnectToParentOnIOThread(ScopedPlatformHandle platform_handle);
  void RequestParentPortConnectionOnIOThread(const ports::PortRef& local_port,
                                             const std::string& token);

  scoped_refptr<NodeChannel> GetPeerChannel(const ports::NodeName& name);
  void AddPeer(const ports::NodeName& name,
               scoped_refptr<NodeChannel> channel,
               bool start_channel);
  void DropPeer(const ports::NodeName& name);
  void SendPeerMessage(const ports::NodeName& name,
                       ports::ScopedMessage message);
  void AcceptIncomingMessages();
  void DropAllPeers();

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
  void OnRequestPortConnection(const ports::NodeName& from_node,
                               const ports::PortName& connector_port_name,
                               const std::string& token) override;
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

  // Guards |peers_| and |pending_peer_messages_|.
  base::Lock peers_lock_;

  // Channels to known peers, including parent and children, if any.
  NodeMap peers_;

  // Outgoing message queues for peers we've heard of but can't yet talk to.
  std::unordered_map<ports::NodeName, OutgoingMessageQueue>
      pending_peer_messages_;

  // Guards |reserved_ports_|.
  base::Lock reserved_ports_lock_;

  // Ports reserved by token.
  base::hash_map<std::string, ports::PortRef> reserved_ports_;

  // All other fields below must only be accessed on the I/O thread, i.e., the
  // thread on which core_->io_task_runner() runs tasks.

  // The name of our parent node, if any.
  ports::NodeName parent_name_;

  // The channel to our parent. This is also stored in |peers_| but kept here
  // for convenient access.
  scoped_refptr<NodeChannel> parent_channel_;

  // Channels to children during handshake.
  NodeMap pending_children_;

  // Port location requests which have been deferred until we have a parent.
  std::vector<PendingPortRequest> pending_port_requests_;

  // Guards |incoming_messages_|.
  base::Lock messages_lock_;
  std::queue<ports::ScopedMessage> incoming_messages_;

  DISALLOW_COPY_AND_ASSIGN(NodeController);
};

}  // namespace edk
}  // namespace mojo

#endif  // MOJO_EDK_SYSTEM_NODE_CONTROLLER_H_
