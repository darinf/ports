// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ports/mojo_system/node.h"

#include "base/logging.h"

namespace mojo {
namespace edk {

Node::~Node() {}

Node::Node() {}

void Node::GenerateRandomPortName(ports::PortName* port_name) {
  GenerateRandomName(port_name);
}

void Node::SendEvent(const ports::NodeName& node, ports::Event event) {
  NOTIMPLEMENTED();
}

void Node::MessagesAvailable(const ports::PortName& port) {
  NOTIMPLEMENTED();
}

}  // namespace edk
}  // namespace mojo
