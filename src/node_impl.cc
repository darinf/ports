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

#include "ports/src/logging.h"

namespace ports {

static int DebugError(const char* message, int error_code, const char* func) {
  CHECK(false) << "Oops: " << message << " @ " << func;
  return error_code;
}
#define Oops(x) DebugError(#x, x, __func__)

static bool CanAcceptMoreMessages(const Port* port) {
  // Have we already doled out the last message (i.e., do we expect to NOT
  // receive further messages)?
  uint32_t next_sequence_num = port->message_queue.next_sequence_num();
  if (port->peer_closed || port->remove_proxy_on_last_message) {
    if (port->last_sequence_num_to_receive == next_sequence_num - 1)
      return false;
  }
  return true;
}

Node::Impl::Impl(const NodeName& name, NodeDelegate* delegate)
    : name_(name),
      delegate_(delegate) {
}

Node::Impl::~Impl() {
  if (!ports_.empty())
    DLOG(WARNING) << "Unclean shutdown for node " << name_;
}

int Node::Impl::CreatePort(PortName* port_name) {
  std::shared_ptr<Port> port = std::make_shared<Port>(kInitialSequenceNum,
                                                      kInitialSequenceNum);
  return AddPort(std::move(port), port_name);
}

int Node::Impl::InitializePort(const PortName& port_name,
                               const NodeName& peer_node_name,
                               const PortName& peer_port_name) {
  std::shared_ptr<Port> port = GetPort(port_name);
  if (!port)
    return ERROR_PORT_UNKNOWN;

  {
    std::lock_guard<std::mutex> guard(port->lock);
    if (port->peer_node_name != kInvalidNodeName ||
        port->peer_port_name != kInvalidPortName)
      return ERROR_PORT_ALREADY_INITIALIZED;

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

int Node::Impl::SetUserData(const PortName& port_name,
                            std::shared_ptr<UserData> user_data) {
  std::shared_ptr<Port> port = GetPort(port_name);
  if (!port)
    return ERROR_PORT_UNKNOWN;
  
  std::lock_guard<std::mutex> guard(port->lock);
  port->user_data = std::move(user_data);

  return OK;
}

int Node::Impl::ClosePort(const PortName& port_name) {
  std::shared_ptr<Port> port = GetPort(port_name);
  if (!port)
    return ERROR_PORT_UNKNOWN;

  {
    std::lock_guard<std::mutex> guard(port->lock);
    if (port->state != Port::kReceiving)
      return ERROR_PORT_STATE_UNEXPECTED;

    ClosePort_Locked(port.get(), port_name);
  }
  ErasePort(port_name);

  return OK;
}

int Node::Impl::GetMessage(const PortName& port_name, ScopedMessage* message) {
  return GetMessageIf(port_name, nullptr, message);
}

int Node::Impl::GetMessageIf(const PortName& port_name,
                             MessageSelector* selector,
                             ScopedMessage* message) {
  *message = nullptr;

  DLOG(INFO) << "GetMessageIf for " << port_name << "@" << name_;

  std::shared_ptr<Port> port = GetPort(port_name);
  if (!port)
    return ERROR_PORT_UNKNOWN;

  {
    std::lock_guard<std::mutex> guard(port->lock);

    // This could also be treated like the port being unknown since the
    // embedder should no longer be referring to a port that has been sent.
    if (port->state != Port::kReceiving)
      return ERROR_PORT_STATE_UNEXPECTED;
  
    // Let the embedder get messages until there are no more before reporting
    // that the peer closed its end.
    if (!CanAcceptMoreMessages(port.get()))
      return ERROR_PORT_PEER_CLOSED;

    port->message_queue.GetNextMessageIf(selector, message);
  }

  // Allow referenced ports to trigger MessagesAvailable calls.
  if (*message) {
    for (size_t i = 0; i < (*message)->num_ports; ++i) {
      std::shared_ptr<Port> new_port = GetPort((*message)->ports[i].name);
      DCHECK(new_port);

      std::lock_guard<std::mutex> guard(new_port->lock);

      DCHECK(new_port->state == Port::kReceiving);
      new_port->message_queue.set_signalable(true);
    }
  }
  return OK;
}

int Node::Impl::SendMessage(const PortName& port_name, ScopedMessage message) {
  for (size_t i = 0; i < message->num_ports; ++i) {
    if (message->ports[i].name == port_name)
      return ERROR_PORT_CANNOT_SEND_SELF;
  }

  std::shared_ptr<Port> port = GetPort(port_name);
  if (!port)
    return ERROR_PORT_UNKNOWN;

  {
    std::lock_guard<std::mutex> guard(port->lock);

    if (port->state != Port::kReceiving)
      return ERROR_PORT_STATE_UNEXPECTED;

    if (port->peer_closed)
      return ERROR_PORT_PEER_CLOSED;

    int rv = SendMessage_Locked(port.get(), port_name, std::move(message));
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
      return PortAccepted(event.port_name);
    case Event::kObserveProxy:
      return ObserveProxy(std::move(event));
    case Event::kObserveProxyAck:
      return ObserveProxyAck(
          event.port_name, event.observe_proxy_ack.last_sequence_num);
    case Event::kObserveClosure:
      return ObserveClosure(std::move(event));
  }
  return Oops(ERROR_NOT_IMPLEMENTED);
}

int Node::Impl::LostConnectionToNode(const NodeName& node_name) {
  // We can no longer send events to the given node. We also can't expect any
  // PortAccepted events.

  DLOG(INFO) << "Observing lost connection from node " << name_
             << " to node " << node_name;

  std::vector<PortName> ports_to_notify;
  std::vector<std::shared_ptr<UserData>> associated_user_data;

  {
    std::lock_guard<std::mutex> guard(ports_lock_);

    for (auto iter = ports_.begin(); iter != ports_.end(); ) {
      std::shared_ptr<Port>& port = iter->second;

      bool remove_port = false;
      {
        std::lock_guard<std::mutex> port_guard(port->lock);

        if (port->peer_node_name == node_name) {
          // We can no longer send messages to this port's peer. We assume we
          // will not receive any more messages from this port's peer as well.
          if (!port->peer_closed) {
            port->peer_closed = true;
            port->last_sequence_num_to_receive =
                port->message_queue.next_sequence_num() - 1;

            if (port->state == Port::kReceiving) {
              ports_to_notify.push_back(iter->first);
              associated_user_data.push_back(port->user_data);
            }
          }

          // We do not expect to forward any further messages, and we do not
          // expect to receive a Port{Accepted,Rejected} event.
          if (port->state != Port::kReceiving)
            remove_port = true;
        }
      }

      if (remove_port) {
        DLOG(INFO) << "Deleted port " << iter->first << "@" << name_;
        iter = ports_.erase(iter);
      } else {
        ++iter;
      }
    }
  }

  for (size_t i = 0; i < ports_to_notify.size(); ++i) {
    delegate_->MessagesAvailable(ports_to_notify[i],
                                 std::move(associated_user_data[i]));
  }

  return OK;
}

int Node::Impl::AcceptMessage(const PortName& port_name,
                              ScopedMessage message) {
#ifndef NDEBUG
  std::ostringstream ports_buf;
  for (size_t i = 0; i < message->num_ports; ++i) {
    if (i > 0)
      ports_buf << ",";
    ports_buf << message->ports[i].name;
  }

  DLOG(INFO) << "AcceptMessage " << message->sequence_num
             << " [ports=" << ports_buf.str() << "]"
             << " at " << port_name << "@" << name_;
#endif

  std::shared_ptr<Port> port = GetPort(port_name);

  // Even if this port does not exist, cannot receive anymore messages or is
  // buffering or proxying messages, we still need these ports to be bound to
  // this node. When the message is forwarded, these ports will get transferred
  // following the usual method. If the message cannot be accepted, then the
  // newly bound ports will simply be closed.

  for (size_t i = 0; i < message->num_ports; ++i) {
    int rv = AcceptPort(message->ports[i]);
    if (rv != OK)
      return rv;
  }

  bool has_next_message = false;
  bool message_accepted = false;
  std::shared_ptr<UserData> associated_user_data;

  if (port) {
    std::lock_guard<std::mutex> guard(port->lock);

    // Reject spurious messages if we've already received the last expected
    // message.
    if (CanAcceptMoreMessages(port.get())) {
      message_accepted = true;
      port->message_queue.AcceptMessage(std::move(message), &has_next_message);

      associated_user_data = port->user_data;

      if (port->state == Port::kBuffering) {
        has_next_message = false;
      } else if (port->state == Port::kProxying) {
        has_next_message = false;

        // Forward messages. We forward messages in sequential order here so
        // that we maintain the message queue's notion of next sequence number.
        // That's useful for the proxy removal process as we can tell when this
        // port has seen all of the messages it is expected to see.
        int rv = ForwardMessages_Locked(port.get(), port_name);
        if (rv != OK)
          return rv;

        MaybeRemoveProxy_Locked(port.get(), port_name);
      }
    }
  }

  if (!message_accepted) {
    DLOG(INFO) << "Message not accepted!\n";
    // Close all newly accepted ports as they are effectively orphaned.
    for (size_t i = 0; i < message->num_ports; ++i)
      ClosePort(message->ports[i].name);
  } else if (has_next_message) {
    delegate_->MessagesAvailable(port_name, std::move(associated_user_data));
  }

  return OK;
}

int Node::Impl::PortAccepted(const PortName& port_name) {
  std::shared_ptr<Port> port = GetPort(port_name);
  if (!port)
    return Oops(ERROR_PORT_UNKNOWN);

  {
    std::lock_guard<std::mutex> guard(port->lock);

    DLOG(INFO) << "PortAccepted at " << port_name << "@" << name_
               << " pointing to "
               << port->peer_port_name << "@" << port->peer_node_name;

    if (port->state != Port::kBuffering)
      return Oops(ERROR_PORT_STATE_UNEXPECTED);

    port->state = Port::kProxying;

    int rv = ForwardMessages_Locked(port.get(), port_name);
    if (rv != OK)
      return rv;

    // We may have observed closure before receiving PortAccepted. In that
    // case, we can advance to removing the proxy without sending out an
    // ObserveProxy message. We already know the last expected message, etc.

    if (port->remove_proxy_on_last_message) {
      MaybeRemoveProxy_Locked(port.get(), port_name);
    } else {
      InitiateProxyRemoval_Locked(port.get(), port_name);
    }
  }
  return OK;
}

int Node::Impl::ObserveProxy(Event event) {
  // The port may have already been closed locally, in which case the
  // ObserveClosure message will contain the last_sequence_num field.
  // We can then silently ignore this message.
  std::shared_ptr<Port> port = GetPort(event.port_name);
  if (!port) {
    DLOG(INFO) << "ObserveProxy: " << event.port_name << "@" << name_
               << " not found";
    return OK;
  }

  DLOG(INFO) << "ObserveProxy at " << event.port_name << "@" << name_
             << ", proxy at "
             << event.observe_proxy.proxy_port_name << "@"
             << event.observe_proxy.proxy_node_name
             << " pointing to "
             << event.observe_proxy.proxy_to_port_name << "@"
             << event.observe_proxy.proxy_to_node_name;

  {
    std::lock_guard<std::mutex> guard(port->lock);

    if (port->peer_node_name == event.observe_proxy.proxy_node_name &&
        port->peer_port_name == event.observe_proxy.proxy_port_name) {
      port->peer_node_name = event.observe_proxy.proxy_to_node_name;
      port->peer_port_name = event.observe_proxy.proxy_to_port_name;

      Event ack(Event::kObserveProxyAck);
      ack.port_name = event.observe_proxy.proxy_port_name;

      if (port->state == Port::kReceiving) {
        ack.observe_proxy_ack.last_sequence_num =
            port->next_sequence_num_to_send - 1;

        delegate_->SendEvent(event.observe_proxy.proxy_node_name,
                             std::move(ack));
      } else {
        // As a proxy ourselves, we don't know how to populate the last
        // sequence num field. Another port could be sending messages to the
        // proxy. Instead, we will send an ObserveProxyAck indicating that the
        // ObserveProxy event should be re-sent. However, this has to be done
        // after we are removed as a proxy. Otherwise, we might just find
        // ourselves back here again, and we don't want to be busy loop.

        ack.observe_proxy_ack.last_sequence_num = kInvalidSequenceNum;

        port->send_on_proxy_removal.emplace(
            std::make_pair(event.observe_proxy.proxy_node_name,
                           std::move(ack)));
      }
    } else {
      // Forward this event along to our peer. Eventually, it should find the
      // port referring to the proxy.
      event.port_name = port->peer_port_name;

      delegate_->SendEvent(port->peer_node_name, std::move(event));
    }
  }
  return OK;
}

int Node::Impl::ObserveProxyAck(const PortName& port_name,
                                uint32_t last_sequence_num) {
  DLOG(INFO) << "ObserveProxyAck at " << port_name << "@" << name_
             << " (last_sequence_num=" << last_sequence_num << ")";

  std::shared_ptr<Port> port = GetPort(port_name);
  if (!port)
    return ERROR_PORT_UNKNOWN;  // The port may have observed closure first, so
                                // this is not an "Oops".

  {
    std::lock_guard<std::mutex> guard(port->lock);

    if (port->state != Port::kProxying)
      return Oops(ERROR_PORT_STATE_UNEXPECTED);

    if (last_sequence_num == kInvalidSequenceNum) {
      // Send again.
      InitiateProxyRemoval_Locked(port.get(), port_name);
      return OK;
    }

    // We can now remove this port once we have received and forwarded the last
    // message addressed to this port.
    port->remove_proxy_on_last_message = true;
    port->last_sequence_num_to_receive = last_sequence_num;

    MaybeRemoveProxy_Locked(port.get(), port_name);
  }
  return OK;
}

int Node::Impl::ObserveClosure(Event event) {
  // OK if the port doesn't exist, as it may have been closed already.
  std::shared_ptr<Port> port = GetPort(event.port_name);
  if (!port)
    return OK;

  // This message tells the port that it should no longer expect more messages
  // beyond last_sequence_num. This message is forwarded along until we reach
  // the receiving end, and this message serves as an equivalent to
  // ObserveProxyAck.

  PortName port_name = event.port_name;
  bool notify_delegate = false;
  std::shared_ptr<UserData> associated_user_data;
  {
    std::lock_guard<std::mutex> guard(port->lock);

    port->peer_closed = true;
    port->last_sequence_num_to_receive =
        event.observe_closure.last_sequence_num;

    DLOG(INFO) << "ObserveClosure at " << event.port_name << "@" << name_
               << " (state=" << port->state << ") pointing to "
               << port->peer_port_name << "@" << port->peer_node_name
               << " (last_sequence_num="
               << event.observe_closure.last_sequence_num << ")";

    if (port->state == Port::kReceiving) {
      if (!CanAcceptMoreMessages(port.get())) {
        notify_delegate = true;
        associated_user_data = port->user_data;
      }
    } else {
      NodeName next_node_name = port->peer_node_name;
      PortName next_port_name = port->peer_port_name;

      port->remove_proxy_on_last_message = true;

      // See about removing the port if it is a proxy as our peer won't be able
      // to participate in proxy removal.
      if (port->state == Port::kProxying) {
        MaybeRemoveProxy_Locked(port.get(), event.port_name);

        // Forward this event along.
        event.port_name = next_port_name;
        delegate_->SendEvent(next_node_name, std::move(event));
      }
    }
  }
  if (notify_delegate)
    delegate_->MessagesAvailable(port_name, std::move(associated_user_data));
  return OK;
}

int Node::Impl::AddPort(std::shared_ptr<Port> port, PortName* port_name) {
  delegate_->GenerateRandomPortName(port_name);
  return AddPortWithName(*port_name, std::move(port));
}

int Node::Impl::AddPortWithName(const PortName& port_name,
                                std::shared_ptr<Port> port) {
  std::lock_guard<std::mutex> guard(ports_lock_);

  if (!ports_.insert(std::make_pair(port_name, port)).second)
    return Oops(ERROR_PORT_EXISTS);  // Suggests a bad UUID generator.

  DLOG(INFO) << "Created port " << port_name << "@" << name_;
  return OK;
}

void Node::Impl::ErasePort(const PortName& port_name) {
  std::lock_guard<std::mutex> guard(ports_lock_);

  ports_.erase(port_name);
  DLOG(INFO) << "Deleted port " << port_name << "@" << name_;
}

std::shared_ptr<Port> Node::Impl::GetPort(const PortName& port_name) {
  std::lock_guard<std::mutex> guard(ports_lock_);

  auto iter = ports_.find(port_name);
  if (iter == ports_.end())
    return std::shared_ptr<Port>();

  return iter->second;
}

void Node::Impl::WillSendPort_Locked(Port* port,
                                     const NodeName& to_node_name,
                                     PortDescriptor* port_descriptor) {
  PortName local_port_name = port_descriptor->name;

  PortName new_port_name;
  delegate_->GenerateRandomPortName(&new_port_name);

  // Make sure we don't send messages to the new peer until after we know it
  // exists. In the meantime, just buffer messages locally.
  DCHECK(port->state == Port::kReceiving);
  port->state = Port::kBuffering;

  port_descriptor->name = new_port_name;
  port_descriptor->peer_node_name = port->peer_node_name;
  port_descriptor->peer_port_name = port->peer_port_name;
  port_descriptor->referring_node_name = name_;
  port_descriptor->referring_port_name = local_port_name;
  port_descriptor->next_sequence_num_to_send = port->next_sequence_num_to_send;
  port_descriptor->next_sequence_num_to_receive =
      port->message_queue.next_sequence_num();

  // Configure the local port to point to the new port.
  port->peer_node_name = to_node_name;
  port->peer_port_name = new_port_name;
}

int Node::Impl::AcceptPort(const PortDescriptor& port_descriptor) {
  std::shared_ptr<Port> port =
      std::make_shared<Port>(port_descriptor.next_sequence_num_to_send,
                             port_descriptor.next_sequence_num_to_receive);
  port->peer_node_name = port_descriptor.peer_node_name;
  port->peer_port_name = port_descriptor.peer_port_name;

  // A newly accepted port is not signalable until the message referencing the
  // new port finds its way to the consumer (see GetMessageIf).
  port->message_queue.set_signalable(false);

  int rv = AddPortWithName(port_descriptor.name, std::move(port));
  if (rv != OK)
    return rv;

  // Allow referring port to forward messages.
  Event event(Event::kPortAccepted);
  event.port_name = port_descriptor.referring_port_name;

  delegate_->SendEvent(port_descriptor.referring_node_name, std::move(event));
  return OK;
}

int Node::Impl::SendMessage_Locked(Port* port,
                                   const PortName& port_name,
                                   ScopedMessage message) {
  message->sequence_num = port->next_sequence_num_to_send++;

#ifndef NDEBUG
  std::ostringstream ports_buf;
  for (size_t i = 0; i < message->num_ports; ++i) {
    if (i > 0)
      ports_buf << ",";
    ports_buf << message->ports[i].name;
  }
#endif

  if (message->num_ports > 0) {
    // Note: Another thread could be trying to send the same ports, so we need
    // to ensure that they are ours to send before we mutate their state.

    std::vector<std::shared_ptr<Port>> ports;
    ports.resize(message->num_ports);

    {
      // Exclude other threads from locking multiple ports in arbitrary order.
      std::lock_guard<std::mutex> guard(send_with_ports_lock_);

      for (size_t i = 0; i < message->num_ports; ++i) {
        ports[i] = GetPort(message->ports[i].name);
        ports[i]->lock.lock();

        if (ports[i]->state != Port::kReceiving) {
          // Oops, we cannot send this port.
          for (size_t j = 0; j <= i; ++j)
            ports[i]->lock.unlock();
          return ERROR_PORT_STATE_UNEXPECTED;
        }
      }
    }

    for (size_t i = 0; i < message->num_ports; ++i) {
      WillSendPort_Locked(
          ports[i].get(), port->peer_node_name, &message->ports[i]);
    }

    for (size_t i = 0; i < message->num_ports; ++i)
      ports[i]->lock.unlock();
  }

#ifndef NDEBUG
  DLOG(INFO) << "Sending message " << message->sequence_num
             << " [ports=" << ports_buf.str() << "]"
             << " from " << port_name << "@" << name_
             << " to " << port->peer_port_name << "@" << port->peer_node_name;
#endif

  Event event(Event::kAcceptMessage);
  event.port_name = port->peer_port_name;
  event.message = std::move(message);

  delegate_->SendEvent(port->peer_node_name, std::move(event));
  return OK;
}

int Node::Impl::ForwardMessages_Locked(Port* port, const PortName &port_name) {
  for (;;) {
    ScopedMessage message;
    port->message_queue.GetNextMessageIf(nullptr, &message);
    if (!message)
      break;

    int rv = SendMessage_Locked(port, port_name, std::move(message));
    if (rv != OK)
      return rv;
  }
  return OK;
}

void Node::Impl::InitiateProxyRemoval_Locked(Port* port,
                                             const PortName& port_name) {
  // To remove this node, we start by notifying the connected graph that we are
  // a proxy. This allows whatever port is referencing this node to skip it.
  // Eventually, this node will receive ObserveProxyAck (or ObserveClosure if
  // the peer was closed in the meantime).

  Event event(Event::kObserveProxy);
  event.port_name = port->peer_port_name;
  event.observe_proxy.proxy_node_name = name_;
  event.observe_proxy.proxy_port_name = port_name;
  event.observe_proxy.proxy_to_node_name = port->peer_node_name;
  event.observe_proxy.proxy_to_port_name = port->peer_port_name;

  delegate_->SendEvent(port->peer_node_name, std::move(event));
}

void Node::Impl::MaybeRemoveProxy_Locked(Port* port,
                                         const PortName& port_name) {
  DCHECK(port->state == Port::kProxying);

  // Make sure we have seen ObserveProxyAck before removing the port.
  if (!port->remove_proxy_on_last_message)
    return;

  if (!CanAcceptMoreMessages(port)) {
    // This proxy port is done. We can now remove it!
    ErasePort(port_name);

    while (!port->send_on_proxy_removal.empty()) {
      std::pair<NodeName, Event>& next = port->send_on_proxy_removal.front();
      delegate_->SendEvent(next.first, std::move(next.second));
      port->send_on_proxy_removal.pop();
    }
  } else {
    DLOG(INFO) << "Cannot remove port " << port_name << "@" << name_
               << " now; waiting for more messages";
  }
}

void Node::Impl::ClosePort_Locked(Port* port, const PortName& port_name) {
  // We pass along the sequence number of the last message sent from this
  // port to allow the peer to have the opportunity to consume all inbound
  // messages before notifying the embedder that this port is closed.

  Event event(Event::kObserveClosure);
  event.port_name = port->peer_port_name;
  event.observe_closure.last_sequence_num = port->next_sequence_num_to_send - 1;

  delegate_->SendEvent(port->peer_node_name, std::move(event));
}

}  // namespace ports
