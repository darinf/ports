// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PORTS_MOJO_SYSTEM_NODE_H_
#define PORTS_MOJO_SYSTEM_NODE_H_

#include "base/macros.h"
#include "crypto/random.h"
#include "ports/include/ports.h"

namespace mojo {
namespace edk {

class Node : public ports::NodeDelegate {
 public:
  ~Node() override;

 protected:
  Node();

  template <typename T>
  void GenerateRandomName(T* out) { crypto::RandBytes(out, sizeof(T)); }

 private:
  // ports::NodeDelegate:
  void GenerateRandomPortName(ports::PortName* port_name) override;
  void SendEvent(const ports::NodeName& node, ports::Event event) override;
  void MessagesAvailable(const ports::PortName& port) override;

  DISALLOW_COPY_AND_ASSIGN(Node);
};

}  // namespace edk
}  // namespace mojo

#endif  // PORTS_MOJO_SYSTEM_NODE_H_
