// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PORTS_MOJO_SYSTEM_CHILD_NODE_CONTROLLER_H_
#define PORTS_MOJO_SYSTEM_CHILD_NODE_CONTROLLER_H_

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "ports/include/ports.h"
#include "ports/mojo_system/node_controller.h"

namespace mojo {
namespace edk {

class Node;

class ChildNodeController : public NodeController {
 public:
  // |node| is not owned and must outlive the controller.
  explicit ChildNodeController(Node* node);
  ~ChildNodeController() override;

 private:
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
  void OnEventMessage(const ports::NodeName& from_node,
                      ports::Event event) override;

  Node* const node_;

  ports::NodeName parent_name_;

  // The channel of the first peer we're asked to accept (i.e. the parent).
  // This is only used until completion of the parent handshake.
  scoped_ptr<NodeChannel> bootstrap_channel_;

  DISALLOW_COPY_AND_ASSIGN(ChildNodeController);
};

}  // namespace edk
}  // namespace mojo

#endif  // PORTS_MOJO_SYSTEM_CHILD_NODE_CONTROLLER_H_
