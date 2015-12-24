// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PORTS_MOJO_SYSTEM_NODE_CONTROLLER_H_
#define PORTS_MOJO_SYSTEM_NODE_CONTROLLER_H_

#include "base/callback_forward.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "ports/include/ports.h"

namespace mojo {
namespace edk {

class NodeChannel;

class NodeController {
 public:
  NodeController() {}
  virtual ~NodeController() {}

  virtual void AcceptPeer(const ports::NodeName& peer_name,
                          scoped_ptr<NodeChannel> channel) {}
  virtual void OnPeerLost(const ports::NodeName& name) {}
  virtual void OnHelloChildMessage(const ports::NodeName& from_node,
                                   const ports::NodeName& parent_name,
                                   const ports::NodeName& token_name) {}
  virtual void OnHelloParentMessage(const ports::NodeName& from_node,
                                    const ports::NodeName& token_name,
                                    const ports::NodeName& child_name) {}
  virtual void OnEventMessage(const ports::NodeName& from_node,
                              ports::Event event) {}
  virtual void OnConnectPortMessage(const ports::NodeName& from_node,
                                    const ports::PortName& child_port_name,
                                    const std::string& token) {}
  virtual void OnConnectPortAckMessage(
      const ports::NodeName& from_node,
      const ports::PortName& child_port_name,
      const ports::PortName& parent_port_name) {}
  virtual void ReservePortForToken(const ports::PortName& port_name,
                                   const std::string& token,
                                   const base::Closure& on_connect) {}
  virtual void ConnectPortByToken(const ports::PortName& port_name,
                                  const std::string& token,
                                  const base::Closure& on_connect) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(NodeController);
};

}  // namespace edk
}  // namespace mojo

#endif  // PORTS_MOJO_SYSTEM_NODE_CONTROLLER_H_
