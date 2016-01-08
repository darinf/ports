// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/edk/system/ports/node_impl.h"

namespace mojo {
namespace edk {
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

int Node::GetUserData(const PortRef& port_ref,
                      std::shared_ptr<UserData>* user_data) {
  return impl_->GetUserData(port_ref, user_data);
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
}  // namespace edk
}  // namespace mojo
