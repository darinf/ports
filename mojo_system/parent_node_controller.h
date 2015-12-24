// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PORTS_MOJO_SYSTEM_PARENT_NODE_CONTROLLER_H_
#define PORTS_MOJO_SYSTEM_PARENT_NODE_CONTROLLER_H_

#include <string>
#include <unordered_map>

#include "base/callback.h"
#include "base/macros.h"
#include "base/synchronization/lock.h"
#include "ports/mojo_system/node_controller.h"
#include "ports/include/ports.h"
#include "ports/src/hash_functions.h"

namespace mojo {
namespace edk {

class Node;

class ParentNodeController : public NodeController {
 public:
  // |node| is not owned and must outlive the controller.
  explicit ParentNodeController(Node* node);
  ~ParentNodeController() override;

 private:
  struct ReservedPort {
    ReservedPort();
    ~ReservedPort();

    ports::PortName local_port;
    base::Closure callback;
  };

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

  Node* const node_;

  base::Lock pending_handshakes_lock_;
  std::unordered_map<ports::NodeName, scoped_ptr<NodeChannel>>
      pending_handshakes_;

  base::Lock reserved_ports_lock_;
  std::unordered_map<std::string, ReservedPort> reserved_ports_;

  DISALLOW_COPY_AND_ASSIGN(ParentNodeController);
};

}  // namespace edk
}  // namespace mojo

#endif  // PORTS_MOJO_SYSTEM_PARENT_NODE_CONTROLLER_H_
