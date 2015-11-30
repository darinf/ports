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

static int DebugError(const char* message, int error_code) {
  printf("*** %s\n", message);
  return error_code;
}
#define Oops(x) DebugError(#x, x)

Node::Impl::Impl(NodeName name, NodeDelegate* delegate)
    : name_(name),
      delegate_(delegate) {
}

Node::Impl::~Impl() {
}

int Node::Impl::AddPort(PortName port_name,
                        PortName peer_name,
                        NodeName peer_node_name) {
  std::shared_ptr<Port> port =
      std::make_shared<Port>(peer_name, peer_node_name, kInitialSequenceNum);

  {
    std::lock_guard<std::mutex> guard(ports_lock_);
    if (!ports_.insert(std::make_pair(port_name, port)).second)
      return Oops(ERROR_PORT_EXISTS);
  }

  return OK;
}

int Node::Impl::CreatePortPair(PortName* port_name_0, PortName* port_name_1) {
  *port_name_0 = delegate_->GeneratePortName();
  *port_name_1 = delegate_->GeneratePortName();

  // A connected pair of ports:
  std::shared_ptr<Port> port0 =
      std::make_shared<Port>(*port_name_1, name_, kInitialSequenceNum);
  std::shared_ptr<Port> port1 =
      std::make_shared<Port>(*port_name_0, name_, kInitialSequenceNum);

  {
    std::lock_guard<std::mutex> guard(ports_lock_);
    ports_.insert(std::make_pair(*port_name_0, port0));
    ports_.insert(std::make_pair(*port_name_1, port1));
  }

  return OK;
}

int Node::Impl::GetMessage(PortName port_name, Message** message) {
  std::shared_ptr<Port> port = GetPort(port_name);
  if (!port)
    return Oops(ERROR_PORT_UNKNOWN);

  {
    std::lock_guard<std::mutex> guard(port->lock);
    port->message_queue.GetNextMessage(message);
  }
  return OK;
}

int Node::Impl::SendMessage(PortName port_name, Message* message) {
  for (size_t i = 0; i < message->num_ports; ++i) {
    if (message->ports[i].name == port_name)
      return Oops(ERROR_PORT_CANNOT_SEND_SELF);
  }

  std::shared_ptr<Port> port = GetPort(port_name);
  if (!port)
    return Oops(ERROR_PORT_UNKNOWN);

  {
    std::lock_guard<std::mutex> guard(port->lock);

    if (port->state == Port::kProxying)
      return Oops(ERROR_PORT_STATE_UNEXPECTED);

    NodeName peer_node_name = port->peer_node_name;
    PortName peer_name = port->peer_name;

    message->sequence_num = port->next_sequence_num++;

    if (message->num_ports > 0) {
      // Remember the ports we are sending so we can continue processing their
      // transfer in AcceptMessageAck. We also need to generate new names for
      // these ports, that the target node will use to refer to them, and we
      // need to populate the PortDescriptor structure with the information
      // needed to setup the port at the target node.

      std::vector<PortName> sent_ports;
      sent_ports.resize(message->num_ports);
      for (size_t i = 0; i < message->num_ports; ++i) {
        sent_ports[i] = message->ports[i].name;
        int rv = WillSendPort(peer_node_name, &message->ports[i]);
        if (rv != OK)
          return rv;
      }

      // We use a separate table for this instead of hanging sent_ports off of
      // the Port structure as the port we are sending the message from may
      // itself be transferred before we receive the AcceptMessageAck.
      {
        std::lock_guard<std::mutex> sent_ports_guard(sent_ports_lock_);
        sent_ports_.emplace(
            std::make_pair(port_name, message->sequence_num),
            std::move(sent_ports));
      }
    }

    delegate_->Send_AcceptMessage(peer_node_name, peer_name, message);
  }
  return OK;
}

int Node::Impl::AcceptMessage(PortName port_name, Message* message) {
  std::shared_ptr<Port> port = GetPort(port_name);
  if (!port)
    return Oops(ERROR_PORT_UNKNOWN);

  // TODO: Need to handle case where port is a proxy!

  for (size_t i = 0; i < message->num_ports; ++i) {
    int rv = AcceptPort(message->ports[i]);
    if (rv != OK)
      return rv;
  }

  bool has_next_message;
  {
    std::lock_guard<std::mutex> guard(port->lock);

    // Only send the Ack when necessary.
    if (message->num_ports > 0) {
      delegate_->Send_AcceptMessageAck(port->peer_node_name,
                                       port->peer_name,
                                       message->sequence_num);
    }

    port->message_queue.AcceptMessage(message, &has_next_message);
  }
  if (has_next_message)
    delegate_->MessagesAvailable(port_name);

  return OK;
}

int Node::Impl::AcceptMessageAck(PortName port_name, uint32_t sequence_num) {
  // Now that the new ports have been setup at the target node, we need to
  // inform the peers of those ports.

  // NOTE: The given port may no longer be bound to this node. That's OK though
  // as we only need to use it as part of the key to the sent_ports_ map.

  std::vector<PortName> port_names;
  {
    std::lock_guard<std::mutex> guard(sent_ports_lock_);

    auto iter = sent_ports_.find(std::make_pair(port_name, sequence_num));
    if (iter != sent_ports_.end()) {
      port_names = std::move(iter->second);
      sent_ports_.erase(iter);
    }
  }

  for (size_t i = 0; i < port_names.size(); ++i) {
    int rv = PortAccepted(port_names[i]);
    if (rv != OK)
      return rv;
  }
  return OK;
}

int Node::Impl::UpdatePort(PortName port_name,
                           PortName peer_name,
                           NodeName peer_node_name) {
  std::shared_ptr<Port> port = GetPort(port_name);
  if (!port)
    return Oops(ERROR_PORT_UNKNOWN);

  {
    std::lock_guard<std::mutex> guard(port->lock);

    port->peer_name = peer_name;
    port->peer_node_name = peer_node_name;

    // If |port| has also (in addition to its peer) been moved, then we need to
    // forward these updates to the new port. We delay sending UpdatePortAck
    // until the new port is updated. That way, our old peer stays around long
    // enough to handle any messages sent to it from the new port.

    if (port->state == Port::kProxying) {
      // TODO: What if the other side has not delivered an AcceptMessageAck?
      // We can't be sure that the other port exists yet!
      delegate_->Send_UpdatePort(port->proxy_to_node_name,
                                 port->proxy_to_port_name,
                                 port->peer_name,
                                 port->peer_node_name);
      port->delayed_update_port_ack = true;
    } else {
      delegate_->Send_UpdatePortAck(port->peer_node_name, port->peer_name);
    }
  }
  return OK;
}

int Node::Impl::UpdatePortAck(PortName port_name) {
  std::shared_ptr<Port> port = GetPort(port_name);
  if (!port)
    return Oops(ERROR_PORT_UNKNOWN);

  // Forward UpdatePortAck corresponding to the forwarded UpdatePort case.
  {
    std::lock_guard<std::mutex> guard(port->lock);
    if (port->delayed_update_port_ack) {
      port->delayed_update_port_ack = false;
      delegate_->Send_UpdatePortAck(port->peer_node_name, port->peer_name);
    }
  }

  // Remove this port as it is now completely moved and no one else should be
  // sending messages to it at this node.
  {
    std::lock_guard<std::mutex> guard(ports_lock_);
    ports_.erase(port_name);
  }
  
  return OK;
}

int Node::Impl::PeerClosed(PortName port_name) {
  // TODO: Implement me.
  printf("*** NOT IMPLEMENTED");
  return -100;
}

std::shared_ptr<Port> Node::Impl::GetPort(PortName port_name) {
  std::lock_guard<std::mutex> guard(ports_lock_);

  auto iter = ports_.find(port_name);
  if (iter == ports_.end())
    return std::shared_ptr<Port>();

  return iter->second;
}

int Node::Impl::WillSendPort(NodeName to_node_name,
                             PortDescriptor* port_descriptor) {
  PortName old_port_name = port_descriptor->name;

  std::shared_ptr<Port> port = GetPort(old_port_name);
  if (!port)
    return Oops(ERROR_PORT_UNKNOWN);

  // Generate a new name for the port. This is done to avoid collisions if the
  // port is later transferred back to this node.
  PortName new_port_name = delegate_->GeneratePortName();

  {
    std::lock_guard<std::mutex> guard(port->lock);

    if (port->state == Port::kProxying) {
      // Oops, the port can only be moved if it is bound to this node.
      return Oops(ERROR_PORT_STATE_UNEXPECTED);
    }

    if (!port->message_queue.IsEmpty()) {
      // TODO: What do we do with any unprocessed messages in this case? Should
      // they also be forwarded? Probably yes.
    }

    port->state = Port::kProxying;
    port->proxy_to_port_name = new_port_name;
    port->proxy_to_node_name = to_node_name;

    port_descriptor->name = new_port_name;
    port_descriptor->peer = port->peer_name;
    port_descriptor->peer_node = port->peer_node_name;
    port_descriptor->next_sequence_num = port->next_sequence_num;
  }
  return OK;
}

int Node::Impl::AcceptPort(const PortDescriptor& port_descriptor) {
  std::shared_ptr<Port> port = GetPort(port_descriptor.name);
  if (port)
    return Oops(ERROR_PORT_EXISTS);

  port = std::make_shared<Port>(port_descriptor.peer,
                                port_descriptor.peer_node,
                                port_descriptor.next_sequence_num);

  {
    std::lock_guard<std::mutex> guard(ports_lock_);
    ports_.insert(std::make_pair(port_descriptor.name, port));
  }
  return OK;
}

int Node::Impl::PortAccepted(PortName port_name) {
  std::shared_ptr<Port> port = GetPort(port_name);
  if (!port)
    return Oops(ERROR_PORT_UNKNOWN);
  
  {
    std::lock_guard<std::mutex> guard(port->lock);

    if (port->state != Port::kProxying)
      return Oops(ERROR_PORT_STATE_UNEXPECTED);

    delegate_->Send_UpdatePort(port->peer_node_name,
                               port->peer_name,
                               port->proxy_to_port_name,
                               port->proxy_to_node_name);
  }
  return OK;
}

}  // namespace ports
