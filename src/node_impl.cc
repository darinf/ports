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

Node::Impl::Impl(NodeName name, NodeDelegate* delegate)
    : name_(name),
      delegate_(delegate) {
}

Node::Impl::~Impl() {
}

int Node::Impl::GetMessage(PortName port_name, Message** message) {
  std::shared_ptr<Port> port = GetPort(port_name);
  if (!port)
    return ERROR;

  // Lock the port before accessing its message queue.
  std::lock_guard<std::mutex> guard(port->lock);
  port->message_queue.GetNextMessage(message);

  return OK;
}

int Node::Impl::SendMessage(PortName port_name, Message* message) {
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
    if (SendPort(peer_node_name, message->dependent_ports[i]) != OK)
      return ERROR;  // Oops!
  }

  message->sequence_num = port->next_sequence_num++;
    
  delegate_->Send_AcceptMessage(peer_node_name, peer_name, message);
  return OK;
}

int Node::Impl::AcceptMessage(PortName port_name, Message* message) {
  std::shared_ptr<Port> port = GetPort(port_name);
  if (!port)
    return ERROR;  // Oops!

  bool has_next_message;
  {
    std::lock_guard<std::mutex> guard(port->lock);
    port->message_queue.AcceptMessage(message, &has_next_message);
  }

  if (has_next_message)
    delegate_->MessagesAvailable(port_name);

  return OK;
}

int Node::Impl::AcceptPort(PortName port_name,
                           PortName peer_name,
                           NodeName peer_node_name,
                           uint32_t next_sequence_num) {
  std::shared_ptr<Port> port = GetPort(port_name);
  if (port)
    return ERROR;  // Oops, port already exists!

  port = std::make_shared<Port>();
  port->peer_name = peer_name;
  port->peer_node_name = peer_node_name;
  port->proxy_to_node_name = 0;
  port->next_sequence_num = next_sequence_num;
  port->is_proxying = false;

  // Hold the port's lock here to ensure that the port or its peer
  // does not get moved after adding the port to the ports table.
  std::lock_guard<std::mutex> port_guard(port->lock);

  {
    std::lock_guard<std::mutex> ports_guard(ports_lock_);
    ports_.insert(std::make_pair(port_name, port));
  }

  delegate_->Send_AcceptPortAck(peer_node_name, port_name);
  return OK;
}

int Node::Impl::AcceptPortAck(PortName port_name) {
  return ERROR;
}

int Node::Impl::UpdatePort(PortName port_name, NodeName peer_node_name) {
  return ERROR;
}

int Node::Impl::UpdatePortAck(PortName port_name) {
  return ERROR;
}

int Node::Impl::PeerClosed(PortName port_name) {
  return ERROR;
}

std::shared_ptr<Port> Node::Impl::GetPort(PortName port_name) {
  std::lock_guard<std::mutex> guard(ports_lock_);

  auto iter = ports_.find(port_name);
  if (iter == ports_.end())
    return std::shared_ptr<Port>();

  return iter->second;
}

int Node::Impl::SendPort(NodeName node_name, PortName port_name) {
  std::shared_ptr<Port> port = GetPort(port_name);

  std::lock_guard<std::mutex> guard(port->lock);

  port->is_proxying = true;
  port->proxy_to_node_name = name_;

  delegate_->Send_AcceptPort(node_name,
                             port_name,
                             port->peer_name,
                             port->peer_node_name,
                             port->next_sequence_num);
  return OK;
}

}  // namespace ports
