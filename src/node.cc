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

#include "node_impl.h"

namespace ports {

Node::Node(NodeDelegate* delegate)
    : impl_(new NodeImpl(delegate)) {
}

Node::~Node() {
  delete impl_;
}

int Node::GetMessage(
    PortName port,
    Message** message) {
  return impl_->GetMessage(port, message);
}

int Node::SendMessage(
    PortName port,
    Message* message) {
  return impl_->SendMessage(port, message);
}

int Node::AcceptMessage(
    PortName port,
    Message* message) {
  return impl_->AcceptMessage(port, message);
}

int Node::AcceptPort(
    PortName port,
    PortName peer,
    NodeName peer_node,
    uint32_t next_sequence_num) {
  return impl_->AcceptPort(port, peer, peer_node, next_sequence_num);
}

int Node::AcceptPortAck(
    PortName port) {
  return impl_->AcceptPortAck(port);
}

int Node::UpdatePort(
    PortName port,
    NodeName peer_node) {
  return impl_->UpdatePort(port, peer_node);
}

int Node::UpdatePortAck(
    PortName port) {
  return impl_->UpdatePortAck(port);
}

int Node::PeerClosed(
    PortName port) {
  return impl_->PeerClosed(port);
}

}  // namespace ports
