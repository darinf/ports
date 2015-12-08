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

#include <cassert>
#include <cstdio>

namespace ports {

static int DebugError(const char* message, int error_code, const char* func) {
#ifndef NDEBUG
  printf("*** %s: %s\n", message, func);
#endif
  return error_code;
}
#define Oops(x) DebugError(#x, x, __func__)

Node::Impl::Impl(NodeName name, NodeDelegate* delegate)
    : name_(name),
      delegate_(delegate) {
}

Node::Impl::~Impl() {
}

int Node::Impl::Shutdown() {
  // TODO:
  return Oops(ERROR_NOT_IMPLEMENTED);
}

int Node::Impl::CreatePort(PortName* port_name) {
  std::shared_ptr<Port> port = std::make_shared<Port>(kInitialSequenceNum);
  return AddPort(std::move(port), port_name);
}

int Node::Impl::InitializePort(PortName port_name,
                               NodeName peer_node_name,
                               PortName peer_port_name) {
  std::shared_ptr<Port> port = GetPort(port_name);
  if (!port)
    return Oops(ERROR_PORT_UNKNOWN);

  {
    std::lock_guard<std::mutex> guard(port->lock);
    if (port->peer_node_name.value != NodeName().value ||
        port->peer_port_name.value != PortName().value)
      return Oops(ERROR_PORT_ALREADY_INITIALIZED);

    port->peer_node_name = peer_node_name;
    port->peer_port_name = peer_port_name;
  }
  return OK;
}

int Node::Impl::CreatePortPair(PortName* port_name_0, PortName* port_name_1) {
  int rv;

  rv = CreatePort(port_name_0);
  if (rv != OK)
    return rv;

  rv = CreatePort(port_name_1);
  if (rv != OK)
    return rv;

  rv = InitializePort(*port_name_0, name_, *port_name_1);
  if (rv != OK)
    return rv;

  rv = InitializePort(*port_name_1, name_, *port_name_0);
  if (rv != OK)
    return rv;

  return OK;
}

int Node::Impl::ClosePort(PortName port_name) {
  std::shared_ptr<Port> port = GetPort(port_name);
  if (!port)
    return Oops(ERROR_PORT_UNKNOWN);

  {
    std::lock_guard<std::mutex> guard(port->lock);
    if (port->state != Port::kReceiving)
      return Oops(ERROR_PORT_STATE_UNEXPECTED);

    port->state = Port::kClosed;

    // We pass along the sequence number of the last message sent from this
    // port to allow the peer to have the opportunity to consume all inbound
    // messages before notifying the embedder that this port is closed.

    Event event(Event::kObserveClosure);
    event.port_name = port->peer_port_name;
    event.observe_closure.closed_node_name = name_;
    event.observe_closure.closed_port_name = port_name;
    event.observe_closure.last_sequence_num = port->next_sequence_num - 1;

    delegate_->SendEvent(port->peer_node_name, std::move(event));
  }
  return OK;
}

int Node::Impl::GetMessage(PortName port_name, ScopedMessage* message) {
  *message = nullptr;

  std::shared_ptr<Port> port = GetPort(port_name);
  if (!port)
    return Oops(ERROR_PORT_UNKNOWN);

  {
    std::lock_guard<std::mutex> guard(port->lock);

    // This could also be treated like the port being unknown since the
    // embedder should no longer be referring to a port that has been sent.
    if (port->state != Port::kReceiving)
      return Oops(ERROR_PORT_STATE_UNEXPECTED);

    port->message_queue.GetNextMessage(message);
  }
  return OK;
}

int Node::Impl::SendMessage(PortName port_name, ScopedMessage message) {
  for (size_t i = 0; i < message->num_ports; ++i) {
    if (message->ports[i].name == port_name)
      return Oops(ERROR_PORT_CANNOT_SEND_SELF);
  }

  std::shared_ptr<Port> port = GetPort(port_name);
  if (!port)
    return Oops(ERROR_PORT_UNKNOWN);

  {
    std::lock_guard<std::mutex> guard(port->lock);

    if (port->state != Port::kReceiving)
      return Oops(ERROR_PORT_STATE_UNEXPECTED);

    int rv = SendMessage_Locked(port.get(), std::move(message));
    if (rv != OK)
      return rv;
  }
  return OK;
}

int Node::Impl::AcceptEvent(Event event) {
  switch (event.type) {
    case Event::kAcceptMessage:
      return AcceptMessage(event.port_name, std::move(event.message));
    case Event::kPortAccepted:
      return PortAccepted(
          event.port_name, event.port_accepted.new_port_name);
    case Event::kPortRejected:
      return PortRejected(event.port_name);
    case Event::kObserveProxy:
      return ObserveProxy(std::move(event));
    case Event::kObserveProxyAck:
      return ObserveProxyAck(
          event.port_name, event.observe_proxy_ack.last_sequence_num);
    case Event::kObserveClosure:
      return ObserveClosure(std::move(event));
    case Event::kObserveClosureAck:
      return ObserveClosureAck(event.port_name);
  }
  return Oops(ERROR_NOT_IMPLEMENTED);
}

int Node::Impl::AddPort(std::shared_ptr<Port> port, PortName* port_name) {
  // Ensure we end up with a unique port name.
  for (;;) {
    *port_name = delegate_->GenerateRandomPortName();

    std::lock_guard<std::mutex> guard(ports_lock_);
    if (ports_.insert(std::make_pair(*port_name, port)).second)
      break;
  }

  printf("Created port %lX@%lX\n", port_name->value, name_.value);
  return OK;
}

std::shared_ptr<Port> Node::Impl::GetPort(PortName port_name) {
  std::lock_guard<std::mutex> guard(ports_lock_);

  auto iter = ports_.find(port_name);
  if (iter == ports_.end())
    return std::shared_ptr<Port>();

  return iter->second;
}

int Node::Impl::AcceptMessage(PortName port_name, ScopedMessage message) {
  std::shared_ptr<Port> port = GetPort(port_name);
  if (!port)
    return Oops(ERROR_PORT_UNKNOWN);

  // If the port is closed, then instead of accepting these ports, we should
  // reject them.
  bool reject_message = false;
  {
    std::lock_guard<std::mutex> guard(port->lock);
    if (port->state == Port::kClosed)
      reject_message = true;
  }
  if (reject_message) {
    for (size_t i = 0; i < message->num_ports; ++i)
      RejectPort(&message->ports[i]);
    return OK;
  }

  // Even if this port is buffering or proxying messages, we still need these
  // ports to be bound to this node. When the message is forwarded, these ports
  // will get transferred following the usual method.

  for (size_t i = 0; i < message->num_ports; ++i) {
    int rv = AcceptPort(&message->ports[i]);
    if (rv != OK)
      return rv;
  }

  bool has_next_message = false;
  {
    std::lock_guard<std::mutex> guard(port->lock);

    port->message_queue.AcceptMessage(std::move(message), &has_next_message);
    if (port->state == Port::kBuffering) {
      has_next_message = false;
    } else if (port->state == Port::kProxying) {
      has_next_message = false;

      // Forward messages. We forward messages in sequential order here so that
      // we maintain the message queue's notion of next sequence number. That's
      // useful for the proxy removal process as we can tell when this port has
      // seen all of the messages it is expected to see.
      int rv = ForwardMessages_Locked(port.get());
      if (rv != OK)
        return rv;

      MaybeRemovePort_Locked(port.get(), port_name);
    }
  }
  if (has_next_message)
    delegate_->MessagesAvailable(port_name);

  return OK;
}

int Node::Impl::WillSendPort(NodeName to_node_name,
                             PortDescriptor* port_descriptor) {
  PortName local_port_name = port_descriptor->name;

  std::shared_ptr<Port> port = GetPort(local_port_name);
  if (!port)
    return Oops(ERROR_PORT_UNKNOWN);

  {
    std::lock_guard<std::mutex> guard(port->lock);

    if (port->state != Port::kReceiving) {
      // Oops, the port can only be moved if it is bound to this node.
      return Oops(ERROR_PORT_STATE_UNEXPECTED);
    }

    NodeName old_peer_node_name = port->peer_node_name;
    PortName old_peer_port_name = port->peer_port_name;

    // Make sure we don't send messages to the new peer until after we know it
    // exists. In the meantime, just buffer messages locally.
    port->state = Port::kBuffering;

    // Our "peer" is now the new port, meaning we will forward messages to the
    // new port.
    port->peer_node_name = to_node_name;
    port->peer_port_name = PortName();  // To be assigned.

    port_descriptor->name = PortName();  // To be assigned.
    port_descriptor->peer_node_name = old_peer_node_name;
    port_descriptor->peer_port_name = old_peer_port_name;
    port_descriptor->referring_node_name = name_;
    port_descriptor->referring_port_name = local_port_name;
    port_descriptor->next_sequence_num = port->next_sequence_num;
  }
  return OK;
}

void Node::Impl::RejectPort(PortDescriptor* port_descriptor) {
  Event event(Event::kPortRejected);
  event.port_name = port_descriptor->referring_port_name;

  delegate_->SendEvent(port_descriptor->referring_node_name, std::move(event));
}

int Node::Impl::PortRejected(PortName port_name) {
  std::shared_ptr<Port> port = GetPort(port_name);
  if (!port)
    return Oops(ERROR_PORT_UNKNOWN);

  {
    std::lock_guard<std::mutex> guard(port->lock);

    if (port->state != Port::kBuffering)
      return Oops(ERROR_PORT_STATE_UNEXPECTED);

    port->state = Port::kProxying;
    port->peer_closed = true;

    // TODO: Deal with queued messages.
    // TODO: Signal to our old peer that we are closed.
  }
  return OK;
}

int Node::Impl::AcceptPort(PortDescriptor* port_descriptor) {
  std::shared_ptr<Port> port =
      std::make_shared<Port>(port_descriptor->next_sequence_num);
  port->peer_node_name = port_descriptor->peer_node_name;
  port->peer_port_name = port_descriptor->peer_port_name;

  PortName port_name;
  int rv = AddPort(std::move(port), &port_name);
  if (rv != OK)
    return rv;

  // Provide the port name here so that it will be visible to the eventual
  // recipient of the message containing this PortDescriptor.
  port_descriptor->name = port_name;

  // Provide the referring port w/ the name of this new port, so it can allow
  // new messages to flow.

  Event event(Event::kPortAccepted);
  event.port_name = port_descriptor->referring_port_name;
  event.port_accepted.new_port_name = port_name;

  delegate_->SendEvent(port_descriptor->referring_node_name, std::move(event));
  return OK;
}

int Node::Impl::PortAccepted(PortName port_name, PortName proxy_to_port_name) {
  std::shared_ptr<Port> port = GetPort(port_name);
  if (!port)
    return Oops(ERROR_PORT_UNKNOWN);

  {
    std::lock_guard<std::mutex> guard(port->lock);

    if (port->state != Port::kBuffering)
      return Oops(ERROR_PORT_STATE_UNEXPECTED);

    port->state = Port::kProxying;
    port->peer_port_name = proxy_to_port_name;

    int rv = ForwardMessages_Locked(port.get());
    if (rv != OK)
      return rv;

    InitiateRemoval_Locked(port.get(), port_name);
  }
  return OK;
}

int Node::Impl::SendMessage_Locked(Port* port, ScopedMessage message) {
  if (port->peer_closed)
    return Oops(ERROR_PORT_PEER_CLOSED);

  message->sequence_num = port->next_sequence_num++;

  for (size_t i = 0; i < message->num_ports; ++i) {
    int rv = WillSendPort(port->peer_node_name, &message->ports[i]);
    if (rv != OK)
      return rv;
  }

  printf("Sending message %u to %lX@%lX\n",
      message->sequence_num,
      port->peer_port_name.value,
      port->peer_node_name.value);

  Event event(Event::kAcceptMessage);
  event.port_name = port->peer_port_name;
  event.message = std::move(message);

  delegate_->SendEvent(port->peer_node_name, std::move(event));
  return OK;
}

int Node::Impl::ForwardMessages_Locked(Port* port) {
  for (;;) {
    ScopedMessage message;
    port->message_queue.GetNextMessage(&message);
    if (!message)
      break;

    int rv = SendMessage_Locked(port, std::move(message));
    if (rv != OK)
      return rv;
  }
  return OK;
}

void Node::Impl::InitiateRemoval_Locked(Port* port, PortName port_name) {
  // To remove this node, we start by notifying the connected graph that we are
  // a proxy. This allows whatever port is referencing this node to skip it.
  // Eventually, this node will receivce an ObserveProxyAck.

  Event event(Event::kObserveProxy);
  event.port_name = port->peer_port_name;
  event.observe_proxy.proxy_node_name = name_;
  event.observe_proxy.proxy_port_name = port_name;
  event.observe_proxy.proxy_to_node_name = port->peer_node_name;
  event.observe_proxy.proxy_to_port_name = port->peer_port_name;

  delegate_->SendEvent(port->peer_node_name, std::move(event));
}

void Node::Impl::MaybeRemovePort_Locked(Port* port, PortName port_name) {
  assert(port->state == Port::kProxying);

  // Make sure we have seen ObserveProxyAck before removing the port.
  if (!port->doomed)
    return;

  uint32_t last_sequence_num_proxied = 
      port->message_queue.next_sequence_num() - 1;

  if (last_sequence_num_proxied == port->last_sequence_num_to_proxy) {
    // This port is done. We can now remove it!
    {
      std::lock_guard<std::mutex> guard(ports_lock_);
      ports_.erase(port_name);
    }
    printf("Deleted port %lX@%lX\n", port_name.value, name_.value);
  }
}  

int Node::Impl::ObserveProxy(Event event) {
  std::shared_ptr<Port> port = GetPort(event.port_name);
  if (!port)
    return Oops(ERROR_PORT_UNKNOWN);
  
  {
    std::lock_guard<std::mutex> guard(port->lock);

    if (port->peer_node_name == event.observe_proxy.proxy_node_name &&
        port->peer_port_name == event.observe_proxy.proxy_port_name) {
      port->peer_node_name = event.observe_proxy.proxy_to_node_name; 
      port->peer_port_name = event.observe_proxy.proxy_to_port_name; 

      Event ack(Event::kObserveProxyAck);
      ack.port_name = event.observe_proxy.proxy_port_name;
      ack.observe_proxy_ack.last_sequence_num = port->next_sequence_num - 1;

      delegate_->SendEvent(event.observe_proxy.proxy_node_name, std::move(ack));
    } else {
      // Forward this event along to our peer. Eventually, it should find the
      // port referring to the proxy.
      event.port_name = port->peer_port_name;

      delegate_->SendEvent(port->peer_node_name, std::move(event));
    }
  }
  return OK;
}

int Node::Impl::ObserveProxyAck(PortName port_name,
                                uint32_t last_sequence_num) {
  std::shared_ptr<Port> port = GetPort(port_name);
  if (!port)
    return Oops(ERROR_PORT_UNKNOWN);

  {
    std::lock_guard<std::mutex> guard(port->lock);

    if (port->state != Port::kProxying)
      return Oops(ERROR_PORT_STATE_UNEXPECTED);

    // We can now remove this port once we have received and forwarded the last
    // message addressed to this port.
    port->doomed = true;
    port->last_sequence_num_to_proxy = last_sequence_num;

    MaybeRemovePort_Locked(port.get(), port_name);
  }
  return OK;
}

int Node::Impl::ObserveClosure(Event event) {
  std::shared_ptr<Port> port = GetPort(event.port_name);
  if (!port)
    return Oops(ERROR_PORT_UNKNOWN);

  {
    std::lock_guard<std::mutex> guard(port->lock);

    if (port->peer_node_name == event.observe_closure.closed_node_name &&
        port->peer_port_name == event.observe_closure.closed_port_name) {
      // TODO: Note that our peer is closed, and do not allow further sending
      // of messages. Signal to the embedder when we receive the last message
      // from our peer.

      port->peer_closed = true;

      Event ack(Event::kObserveClosureAck);
      ack.port_name = port->peer_port_name;

      delegate_->SendEvent(port->peer_node_name, std::move(ack));
    } else {
      // Forward this event along to our peer. Eventually, it should find the
      // port referring to the closed port.
      event.port_name = port->peer_port_name;

      delegate_->SendEvent(port->peer_node_name, std::move(event));
    }
  }
  return OK;
}

int Node::Impl::ObserveClosureAck(PortName port_name) {
  std::shared_ptr<Port> port = GetPort(port_name);
  if (!port)
    return Oops(ERROR_PORT_UNKNOWN);

  // TODO: Do we need to keep this around much as if it were a proxy to help
  // reject incoming messages with ports?

  return OK;
}

}  // namespace ports
