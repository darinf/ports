// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PORTS_MOJO_SYSTEM_NODE_H_
#define PORTS_MOJO_SYSTEM_NODE_H_

#include <queue>
#include <unordered_map>

#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/synchronization/lock.h"
#include "mojo/edk/embedder/platform_handle_vector.h"
#include "mojo/edk/embedder/scoped_platform_handle.h"
#include "ports/include/ports.h"
#include "ports/mojo_system/node_channel.h"
#include "ports/src/hash_functions.h"

namespace mojo {
namespace edk {

class Core;

class Node : public ports::NodeDelegate, public NodeChannel::Delegate {
 public:
  class PortObserver : public ports::UserData {
   public:
    ~PortObserver() override {}
    virtual void OnMessagesAvailable() = 0;
  };

  // |core| owns and out-lives us.
  explicit Node(Core* core);
  ~Node() override;

  const ports::NodeName& name() const { return name_; }
  Core* core() const { return core_; }

  // Connects this node to a child node. This node will initiate a handshake.
  void ConnectToChild(ScopedPlatformHandle platform_handle);

  // Connects this node to a parent node. The parent node will initiate a
  // handshake.
  void ConnectToParent(ScopedPlatformHandle platform_handle);

  // Connects this node to a via an OS pipe under |platform_handle|.
  void ConnectToPeer(const ports::NodeName& peer_name,
                     ScopedPlatformHandle platform_handle);

  // Registers a peer named |name| with a new NodeChannel established over
  // |platform_handle|. |name| must be a valid node name. If |start_channel|
  // is true, |channel| will be started immediately after it's added as a peer.
  void AddPeer(const ports::NodeName& name,
               scoped_ptr<NodeChannel> channel,
               bool start_channel);

  // Drops the connection to peer named |name| if one exists.
  void DropPeer(const ports::NodeName& name);

  // Sends a ports::ScopedMessage to another node, or queues it for delivery if
  // we don't yet know how to talk to that node.
  void SendPeerMessage(const ports::NodeName& name,
                       ports::ScopedMessage message);

  // Creates a single uninitialized port which is not ready for use.
  void CreateUninitializedPort(ports::PortName* port_name);

  // Initializes a port with peer information.
  void InitializePort(const ports::PortName& port_name,
                      const ports::NodeName& peer_node_name,
                      const ports::PortName& peer_port_name);

  // Creates a new pair of local ports on this node, returning their names.
  void CreatePortPair(ports::PortName* port0, ports::PortName* port1);

  // Sets a port's observer.
  void SetPortObserver(const ports::PortName& port_name,
                       std::shared_ptr<PortObserver> observer);

  int AllocMessage(size_t num_payload_bytes,
                   size_t num_ports,
                   ScopedPlatformHandleVectorPtr platform_handles,
                   ports::ScopedMessage* message);

  // Sends a message on a port to its peer.
  int SendMessage(const ports::PortName& port_name,
                  ports::ScopedMessage message);

  // Enable use of lambda functions for selecting messages.
  template <typename Predicate>
  int GetMessageIf(const ports::PortName& port_name,
                   Predicate predicate,
                   ports::ScopedMessage* message) {
    class Adaptor : public ports::MessageSelector {
     public:
      explicit Adaptor(Predicate predicate) : predicate_(predicate) {}
      bool Select(const ports::Message& message) override {
        return predicate_(message);
      }
      Predicate predicate_;
    } adaptor(predicate);
    return node_->GetMessageIf(port_name, &adaptor, message);
  }

  // Closes a port.
  void ClosePort(const ports::PortName& port_name);

  void ReservePortForToken(const ports::PortName& port_name,
                           const std::string& token,
                           const base::Closure& on_connect);

  void ConnectToParentPortByToken(const std::string& token,
                                  const ports::PortName& local_port,
                                  const base::Closure& on_connect);

 private:
  using NodeMap = std::unordered_map<ports::NodeName, scoped_ptr<NodeChannel>>;
  using OutgoingMessageQueue = std::queue<ports::ScopedMessage>;

  struct PendingTokenConnection {
    PendingTokenConnection();
    ~PendingTokenConnection();

    ports::PortName port;
    std::string token;
    base::Closure callback;
  };

  struct ReservedPort {
    ReservedPort();
    ~ReservedPort();

    ports::PortName local_port;
    base::Closure callback;
  };

  void AddPeerNoLock(const ports::NodeName& name,
                     scoped_ptr<NodeChannel> channel,
                     bool start_channel);
  void DropPeerNoLock(const ports::NodeName& name);
  void ConnectToParentPortByTokenNowNoLock(const std::string& token,
                                           const ports::PortName& local_port,
                                           const base::Closure& on_connect);
  void AcceptMessageOnIOThread(ports::ScopedMessage message);

  // ports::NodeDelegate:
  void GenerateRandomPortName(ports::PortName* port_name) override;
  void AllocMessage(size_t num_header_bytes,
                    size_t num_payload_bytes,
                    size_t num_ports,
                    ports::ScopedMessage* message) override;
  void ForwardMessage(const ports::NodeName& node,
                      ports::ScopedMessage message) override;
  void MessagesAvailable(const ports::PortName& port,
                         std::shared_ptr<ports::UserData> user_data) override;

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
                       const ports::PortName& connector_port,
                       const std::string& token) override;
  void OnConnectToPortAck(const ports::NodeName& from_node,
                          const ports::PortName& connector_port,
                          const ports::PortName& connectee_port) override;
  void OnRequestIntroduction(const ports::NodeName& from_node,
                             const ports::NodeName& name) override;
  void OnIntroduce(const ports::NodeName& from_node,
                   const ports::NodeName& name,
                   ScopedPlatformHandle channel_handle) override;
  void OnChannelError(const ports::NodeName& from_node) override;

  // These are safe to access from any thread without locking as long as the
  // Node is alive.
  Core* const core_;
  const ports::NodeName name_;
  const scoped_ptr<ports::Node> node_;

  // Guards access to all of the fields below.
  base::Lock lock_;

  // The name of our parent node, if any.
  ports::NodeName parent_name_;

  // A channel to our parent during handshake.
  scoped_ptr<NodeChannel> bootstrap_channel_to_parent_;

  // Channels to known peers, including parent and children, if any.
  NodeMap peers_;

  // Channels to children during handshake.
  NodeMap pending_children_;

  // Named ports for establishing cross-node port pairs out-of-band. A port
  // can be reserved by name via ReservePortForToken(), and a peer can entangle
  // one of its owns ports to the reserved port by referencing the token in a
  // NodeChannel::ConnectToPort request.
  //
  // The embedder must provide a channel to communicate the token to each node.
  std::unordered_map<std::string, ReservedPort> reserved_ports_;

  // This tracks pending outgoing connection request for named ports.
  std::vector<PendingTokenConnection> pending_token_connections_;
  std::unordered_map<ports::PortName, base::Closure> pending_connection_acks_;

  // Outgoing message queues for peers we've heard of but can't yet talk to.
  std::unordered_map<ports::NodeName, OutgoingMessageQueue>
      pending_peer_messages_;

  DISALLOW_COPY_AND_ASSIGN(Node);
};

}  // namespace edk
}  // namespace mojo

#endif  // PORTS_MOJO_SYSTEM_NODE_H_
