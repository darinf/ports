// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PORTS_MOJO_SYSTEM_NODE_H_
#define PORTS_MOJO_SYSTEM_NODE_H_

#include <unordered_map>

#include "base/macros.h"
#include "base/synchronization/lock.h"
#include "crypto/random.h"
#include "ports/include/ports.h"
#include "ports/mojo_system/node_channel.h"
#include "ports/src/hash_functions.h"

namespace mojo {
namespace edk {

class Node : public ports::NodeDelegate {
 public:
  ~Node() override;

 protected:
  Node();

  template <typename T>
  void GenerateRandomName(T* out) { crypto::RandBytes(out, sizeof(T)); }

  void AddPeer(const ports::NodeName& name, scoped_ptr<NodeChannel> channel);
  NodeChannel* GetPeer(const ports::NodeName& name);

 private:
  // ports::NodeDelegate:
  void GenerateRandomPortName(ports::PortName* port_name) override;
  void SendEvent(const ports::NodeName& node, ports::Event event) override;
  void MessagesAvailable(const ports::PortName& port) override;

  base::Lock peers_lock_;
  std::unordered_map<ports::NodeName, scoped_ptr<NodeChannel>> peers_;

  DISALLOW_COPY_AND_ASSIGN(Node);
};

}  // namespace edk
}  // namespace mojo

#endif  // PORTS_MOJO_SYSTEM_NODE_H_
