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

Node::Node(const NodeName& name, NodeDelegate* delegate)
    : impl_(new Impl(name, delegate)) {
}

Node::~Node() {
  delete impl_;
}

int Node::GetPort(const PortName& port_name, PortRef* port_ref) {
  return impl_->GetPort(port_name, port_ref);
}

int Node::CreateUninitializedPort(PortRef* port_ref) {
  return impl_->CreateUninitializedPort(port_ref);
}

int Node::InitializePort(const PortRef& port_ref,
                         const NodeName& peer_node_name,
                         const PortName& peer_port_name) {
  return impl_->InitializePort(port_ref, peer_node_name, peer_port_name);
}

int Node::CreatePortPair(PortRef* port0_ref, PortRef* port1_ref) {
  return impl_->CreatePortPair(port0_ref, port1_ref);
}

int Node::SetUserData(const PortRef& port_ref,
                      std::shared_ptr<UserData> user_data) {
  return impl_->SetUserData(port_ref, std::move(user_data));
}

int Node::ClosePort(const PortRef& port_ref) {
  return impl_->ClosePort(port_ref);
}

int Node::GetMessage(const PortRef& port_ref, ScopedMessage* message) {
  return impl_->GetMessage(port_ref, message);
}

int Node::GetMessageIf(const PortRef& port_ref,
                       MessageSelector* selector,
                       ScopedMessage* message) {
  return impl_->GetMessageIf(port_ref, selector, message);
}

int Node::AllocMessage(size_t num_payload_bytes,
                       size_t num_ports,
                       ScopedMessage* message) {
  return impl_->AllocMessage(num_payload_bytes, num_ports, message);
}

int Node::SendMessage(const PortRef& port_ref, ScopedMessage message) {
  return impl_->SendMessage(port_ref, std::move(message));
}

int Node::AcceptMessage(ScopedMessage message) {
  return impl_->AcceptMessage(std::move(message));
}

int Node::LostConnectionToNode(const NodeName& node) {
  return impl_->LostConnectionToNode(node);
}

}  // namespace ports
