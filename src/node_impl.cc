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

Node::Impl::Impl(NodeDelegate* delegate)
    : delegate_(delegate) {
}

Node::Impl::~Impl() {
}

int Node::Impl::GetMessage(
    PortName port_name,
    Message** message) {
  std::shared_ptr<Port> port = GetPort(port_name);
  if (!port)
    return ERROR;

  // Lock the port before accessing its message queue.
  std::lock_guard<std::mutex> guard(port->lock);
  return port->message_queue.GetMessage(message);
}

int Node::Impl::SendMessage(
    PortName port_name,
    Message* message) {
  for (size_t i = 0; i < message->num_dependent_ports; ++i) {
    if (message->dependent_ports[i] == port_name)
      return ERROR;
  }

  std::shared_ptr<Port> port = GetPort(port_name);
  if (!port)
    return ERROR;

  std::lock_guard<std::mutex> guard(port->lock);
  if (port->is_proxying)
    return ERROR;

  NodeName peer_node_name = port->peer_node_name;
  PortName peer_name = port->peer_name;

  // Call SendPort here while holding the port's lock to ensure that
  // peer_node_name doesn't change.
  for (size_t i = 0; i < message->num_dependent_ports; ++i) {
    if (SendPort(peer_node_name, message->dependent_ports[i]) != OK) {
      // Oops!
      return ERROR;
    }
  }

  message->sequence_num = port->next_sequence_num++;
    
  return delegate_->Send_AcceptMessage(peer_node_name, peer_name, message);
}

int Node::Impl::AcceptMessage(
    PortName port_name,
    Message* message) {
  return ERROR;
}

int Node::Impl::AcceptPort(
    PortName port_name,
    PortName peer_name,
    NodeName peer_node_name,
    uint32_t next_sequence_num) {
  return ERROR;
}

int Node::Impl::AcceptPortAck(
    PortName port_name) {
  return ERROR;
}

int Node::Impl::UpdatePort(
    PortName port_name,
    NodeName peer_node_name) {
  return ERROR;
}

int Node::Impl::UpdatePortAck(
    PortName port_name) {
  return ERROR;
}

int Node::Impl::PeerClosed(
    PortName port_name) {
  return ERROR;
}

std::shared_ptr<Port> Node::Impl::GetPort(PortName port_name) {
  std::lock_guard<std::mutex> guard(ports_lock_);

  auto iter = ports_.find(port_name);
  if (iter == ports_.end())
    return std::shared_ptr<Port>();

  return iter->second;
}

}  // namespace ports
