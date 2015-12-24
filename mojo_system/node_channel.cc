// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ports/mojo_system/node_channel.h"

#include <cstring>
#include <limits>
#include <sstream>

#include "base/logging.h"
#include "ports/mojo_system/channel.h"

namespace mojo {
namespace edk {

NodeChannel::IncomingMessage::IncomingMessage(
    Channel::IncomingMessage* message)
    : data_(message->payload_size()),
      handles_(message->TakeHandles()) {
  memcpy(data_.data(), message->payload(), message->payload_size());
}

NodeChannel::IncomingMessage::~IncomingMessage() {}

NodeChannel::OutgoingMessage::OutgoingMessage(
    MessageType type,
    size_t payload_size,
    ScopedPlatformHandleVectorPtr handles)
    : message_(new Channel::OutgoingMessage(
        nullptr, sizeof(MessageHeader) + payload_size, std::move(handles))) {
  header()->type = type;
  header()->padding = 0;
}

NodeChannel::OutgoingMessage::~OutgoingMessage() {}

// static
NodeChannel::OutgoingMessagePtr NodeChannel::NewHelloChildMessage(
    const ports::NodeName& parent_name,
    const ports::NodeName& token_name) {
  OutgoingMessagePtr message(new OutgoingMessage(
      MessageType::HELLO_CHILD, sizeof(HelloChildMessageData), nullptr));
  HelloChildMessageData* data = message->payload<HelloChildMessageData>();
  data->parent_name = parent_name;
  data->token_name = token_name;
  return message;
}

// static
NodeChannel::OutgoingMessagePtr NodeChannel::NewHelloParentMessage(
    const ports::NodeName& token_name,
    const ports::NodeName& child_name) {
  OutgoingMessagePtr message(new OutgoingMessage(
      MessageType::HELLO_PARENT, sizeof(HelloParentMessageData), nullptr));
  HelloParentMessageData* data = message->payload<HelloParentMessageData>();
  data->token_name = token_name;
  data->child_name = child_name;
  return message;
}

// static
NodeChannel::OutgoingMessagePtr NodeChannel::NewEventMessage(
    ports::Event event) {
  size_t event_size = sizeof(EventMessageData);
  size_t message_size = 0;
  if (event.message) {
    DCHECK(event.type == ports::Event::kAcceptMessage);
    message_size = sizeof(ports::Message) + event.message->num_bytes +
        event.message->num_ports * sizeof(ports::PortDescriptor);
    event_size += message_size;
  }

  OutgoingMessagePtr message(
      new OutgoingMessage(MessageType::EVENT, event_size, nullptr));
  EventMessageData* data = message->payload<EventMessageData>();
  data->type = event.type;
  data->port_name = event.port_name;
  switch (event.type) {
    case ports::Event::kAcceptMessage:
      memcpy(&data[1], event.message.get(), message_size);
      break;
    case ports::Event::kPortAccepted:
      break;
    case ports::Event::kObserveProxy:
      memcpy(&data->observe_proxy, &event.observe_proxy,
          sizeof(event.observe_proxy));
      break;
    case ports::Event::kObserveProxyAck:
      memcpy(&data->observe_proxy_ack, &event.observe_proxy_ack,
          sizeof(event.observe_proxy_ack));
      break;
    case ports::Event::kObserveClosure:
      memcpy(&data->observe_closure, &event.observe_closure,
          sizeof(event.observe_closure));
      break;
    default:
      NOTREACHED() << "Unknown event type: " << event.type;
  }

  return message;
}

NodeChannel::OutgoingMessagePtr NodeChannel::NewConnectPortMessage(
    const ports::PortName& child_port_name,
    const std::string& token) {
  OutgoingMessagePtr message(
      new OutgoingMessage(MessageType::CONNECT_PORT,
                          sizeof(ConnectPortMessageData) + token.size(),
                          nullptr));
  ConnectPortMessageData* data = message->payload<ConnectPortMessageData>();
  data->child_port_name = child_port_name;
  memcpy(&data[1], token.data(), token.size());
  return message;
}

NodeChannel::OutgoingMessagePtr NodeChannel::NewConnectPortAckMessage(
    const ports::PortName& child_port_name,
    const ports::PortName& parent_port_name) {
  OutgoingMessagePtr message(
      new OutgoingMessage(MessageType::CONNECT_PORT_ACK,
                          sizeof(ConnectPortAckMessageData), nullptr));
  ConnectPortAckMessageData* data =
      message->payload<ConnectPortAckMessageData>();
  data->child_port_name = child_port_name;
  data->parent_port_name = parent_port_name;
  return message;
}

NodeChannel::NodeChannel(Delegate* delegate,
                         ScopedPlatformHandle platform_handle,
                         scoped_refptr<base::TaskRunner> io_task_runner)
    : delegate_(delegate),
      channel_(
          Channel::Create(this, std::move(platform_handle), io_task_runner)) {}

NodeChannel::~NodeChannel() {
  channel_->ShutDown();
}

void NodeChannel::Start() {
  channel_->Start();
}

void NodeChannel::SetRemoteNodeName(const ports::NodeName& name) {
  base::AutoLock lock(name_lock_);
  remote_node_name_ = name;
}

void NodeChannel::SendMessage(OutgoingMessagePtr node_message) {
  channel_->Write(node_message->TakeMessage());
}

void NodeChannel::OnChannelRead(Channel::IncomingMessage* message) {
  ports::NodeName remote_name;
  {
    base::AutoLock lock(name_lock_);
    remote_name = remote_node_name_;
  }
  delegate_->OnMessageReceived(remote_name,
                               make_scoped_ptr(new IncomingMessage(message)));
}

void NodeChannel::OnChannelError() {
  delegate_->OnChannelError(remote_node_name_);
}

std::ostream& operator<<(std::ostream& stream,
                         NodeChannel::MessageType message_type) {
  switch (message_type) {
    case NodeChannel::MessageType::HELLO_CHILD:
      stream << "HELLO_CHILD";
      break;
    case NodeChannel::MessageType::HELLO_PARENT:
      stream << "HELLO_PARENT";
      break;
    case NodeChannel::MessageType::EVENT:
      stream << "EVENT";
      break;
    case NodeChannel::MessageType::CONNECT_PORT:
      stream << "CONNECT_PORT";
      break;
    case NodeChannel::MessageType::CONNECT_PORT_ACK:
      stream << "CONNECT_PORT_ACK";
      break;
    default:
      stream << "UNKNOWN(" << static_cast<int>(message_type) << ")";
      break;
  }
  return stream;
}

}  // namespace edk
}  // namespace mojo
