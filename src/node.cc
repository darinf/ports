// Copyright 2015, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "ports/src/node_impl.h"

namespace ports {

Node::Node(NodeName name, NodeDelegate* delegate)
    : impl_(new Impl(name, delegate)) {
}

Node::~Node() {
  delete impl_;
}

int Node::CreatePort(PortName* port) {
  return impl_->CreatePort(port);
}

int Node::InitializePort(PortName port, NodeName peer_node, PortName peer) {
  return impl_->InitializePort(port, peer_node, peer);
}

int Node::CreatePortPair(PortName* port0, PortName* port1) {
  return impl_->CreatePortPair(port0, port1);
}

int Node::ClosePort(PortName port_name) {
  return impl_->ClosePort(port_name);
}

int Node::GetMessage(PortName port, ScopedMessage* message) {
  return impl_->GetMessage(port, message);
}

int Node::SendMessage(PortName port, ScopedMessage message) {
  return impl_->SendMessage(port, std::move(message));
}

int Node::AcceptEvent(Event event) {
  return impl_->AcceptEvent(std::move(event));
}

int Node::LostConnectionToNode(NodeName node) {
  return impl_->LostConnectionToNode(node);
}

}  // namespace ports
