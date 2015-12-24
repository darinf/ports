// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PORTS_MOJO_SYSTEM_CHILD_NODE_CONTROLLER_H_
#define PORTS_MOJO_SYSTEM_CHILD_NODE_CONTROLLER_H_

#include <string>
#include <unordered_map>
#include <vector>

#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "base/synchronization/lock.h"
#include "ports/include/ports.h"
#include "ports/mojo_system/node_controller.h"
#include "ports/src/hash_functions.h"

namespace mojo {
namespace edk {

class Node;

class ChildNodeController : public NodeController {
 public:
  // |node| is not owned and must outlive the controller.
  explicit ChildNodeController(Node* node);
  ~ChildNodeController() override;

 private:
  struct PendingTokenConnection {
    PendingTokenConnection();
    ~PendingTokenConnection();

    ports::PortName port;
    std::string token;
    base::Closure callback;
  };

  bool IsConnected() const;

  // NodeController:
  void AcceptPeer(const ports::NodeName& peer_name,
                  scoped_ptr<NodeChannel> channel) override;
  void OnPeerLost(const ports::NodeName& name) override;
  void OnHelloChildMessage(const ports::NodeName& from_node,
                           const ports::NodeName& parent_name,
                           const ports::NodeName& token_name) override;
  void OnHelloParentMessage(const ports::NodeName& from_node,
                            const ports::NodeName& token_name,
                            const ports::NodeName& child_name) override;
  void OnConnectPortMessage(const ports::NodeName& from_node,
                            const ports::PortName& child_port_name,
                            const std::string& token) override;
  void OnConnectPortAckMessage(
      const ports::NodeName& from_node,
      const ports::PortName& child_port_name,
      const ports::PortName& parent_port_name) override;
  void ReservePortForToken(const ports::PortName& port_name,
                           const std::string& token,
                           const base::Closure& on_connect) override;
  void ConnectPortByToken(const ports::PortName& port_name,
                          const std::string& token,
                          const base::Closure& on_connect) override;

  void ConnectPortByTokenNow(const ports::PortName& port_name,
                             const std::string& token,
                             const base::Closure& on_connect);

  Node* const node_;

  ports::NodeName parent_name_;

  // The channel of the first peer we're asked to accept (i.e. the parent).
  // This is only used until completion of the parent handshake.
  scoped_ptr<NodeChannel> bootstrap_channel_;

  // We have to keep track of pending token connection requests until we've
  // established channel to the parent node.
  base::Lock pending_token_connections_lock_;
  std::vector<PendingTokenConnection> pending_token_connections_;
  std::unordered_map<ports::PortName, base::Closure> pending_connection_acks_;

  DISALLOW_COPY_AND_ASSIGN(ChildNodeController);
};

}  // namespace edk
}  // namespace mojo

#endif  // PORTS_MOJO_SYSTEM_CHILD_NODE_CONTROLLER_H_
