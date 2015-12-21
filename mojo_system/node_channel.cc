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

NodeChannel::Message::Message(std::vector<char> data,
                              ScopedPlatformHandleVectorPtr handles)
    : data_(std::move(data)), handles_(std::move(handles)) {
  // TODO: Remove gross hacks to fix up pointers in event message data.
  if (header()->type == Type::EVENT) {
    EventData* data = reinterpret_cast<EventData*>(payload());
    if (data->type == ports::Event::kAcceptMessage) {
      char* message_data = static_cast<char*>(payload()) + sizeof(EventData);
      data->message = reinterpret_cast<ports::Message*>(message_data);
      data->message->ports = reinterpret_cast<ports::PortDescriptor*>(
          message_data + sizeof(ports::Message));
      data->message->bytes = reinterpret_cast<char*>(data->message->ports) +
          data->message->num_ports * sizeof(ports::PortDescriptor);
    }
  }
}

NodeChannel::Message::~Message() {}

NodeChannel::Message::Message(Type type,
                              size_t num_bytes,
                              ScopedPlatformHandleVectorPtr handles)
    : data_(sizeof(Header) + num_bytes), handles_(std::move(handles)) {
  Header* header = reinterpret_cast<Header*>(data_.data());
  header->num_bytes = num_bytes;
  header->num_handles = handles_ ? handles_->size() : 0;
  header->type = type;
  header->padding = 0;
}

// static
NodeChannel::MessagePtr NodeChannel::Message::NewHelloChildMessage(
    const ports::NodeName& parent_name,
    const ports::NodeName& token_name) {
  MessagePtr message(
      new Message(Type::HELLO_CHILD, sizeof(HelloChildData), nullptr));
  HelloChildData* data = reinterpret_cast<HelloChildData*>(message->payload());
  data->parent_name = parent_name;
  data->token_name = token_name;
  return message;
}

// static
NodeChannel::MessagePtr NodeChannel::Message::NewHelloParentMessage(
    const ports::NodeName& token_name,
    const ports::NodeName& child_name) {
  MessagePtr message(
      new Message(Type::HELLO_PARENT, sizeof(HelloParentData), nullptr));
  HelloParentData* data =
      reinterpret_cast<HelloParentData*>(message->payload());
  data->token_name = token_name;
  data->child_name = child_name;
  return message;
}

// static
NodeChannel::MessagePtr NodeChannel::Message::NewEventMessage(
    ports::Event event) {
  size_t event_size = sizeof(EventData);
  size_t message_size = 0;
  if (event.message) {
    DCHECK(event.type == ports::Event::kAcceptMessage);
   message_size = sizeof(ports::Message) + event.message->num_bytes +
        event.message->num_ports * sizeof(ports::PortDescriptor);
    event_size += message_size;
  }

  MessagePtr message(new Message(Type::EVENT, event_size, nullptr));
  EventData* data = reinterpret_cast<EventData*>(message->payload());
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

const NodeChannel::Message::HelloChildData&
NodeChannel::Message::AsHelloChild() const {
  DCHECK(header()->type == Type::HELLO_CHILD);
  return *reinterpret_cast<const HelloChildData*>(payload());
}

const NodeChannel::Message::HelloParentData&
NodeChannel::Message::AsHelloParent() const {
  DCHECK(header()->type == Type::HELLO_PARENT);
  return *reinterpret_cast<const HelloParentData*>(payload());
}

const NodeChannel::Message::EventData& NodeChannel::Message::AsEvent() const {
  DCHECK(header()->type == Type::EVENT);
  return *reinterpret_cast<const EventData*>(payload());
}

NodeChannel::NodeChannel(Delegate* delegate,
                         ScopedPlatformHandle platform_handle,
                         scoped_refptr<base::TaskRunner> io_task_runner)
    : delegate_(delegate),
      channel_(
          Channel::Create(this, std::move(platform_handle), io_task_runner)) {}

NodeChannel::~NodeChannel() {}

void NodeChannel::SetRemoteNodeName(const ports::NodeName& name) {
  base::AutoLock lock(name_lock_);
  remote_node_name_ = name;
}

void NodeChannel::SendMessage(MessagePtr node_message) {
  Channel::OutgoingMessagePtr message(new Channel::OutgoingMessage(
      node_message->data(), node_message->num_bytes(),
      node_message->TakeHandles()));
  channel_->Write(std::move(message));
}

void NodeChannel::OnChannelRead(Channel::IncomingMessage* message) {
  std::vector<char> bytes(message->payload_size());
  memcpy(bytes.data(), message->payload(), message->payload_size());
  ScopedPlatformHandleVectorPtr handles = message->TakeHandles();

  ports::NodeName remote_name;
  {
    base::AutoLock lock(name_lock_);
    remote_name = remote_node_name_;
  }

  DLOG(INFO) << "Channel to " << remote_name << " received message with "
      << bytes.size() << " bytes and " << (handles ? handles->size() : 0)
      << " handles.";

  delegate_->OnMessageReceived(
      remote_name,
      make_scoped_ptr(new Message(std::move(bytes), std::move(handles))));
}

void NodeChannel::OnChannelError() {
  delegate_->OnChannelError(remote_node_name_);
}

std::ostream& operator<<(std::ostream& stream,
                         NodeChannel::Message::Type message_type) {
  switch (message_type) {
    case NodeChannel::Message::Type::HELLO_CHILD:
      stream << "HELLO_CHILD";
      break;
    case NodeChannel::Message::Type::HELLO_PARENT:
      stream << "HELLO_PARENT";
      break;
    case NodeChannel::Message::Type::EVENT:
      stream << "EVENT";
      break;
    default:
      stream << "UNKNOWN(" << static_cast<int>(message_type) << ")";
      break;
  }
  return stream;
}

}  // namespace edk
}  // namespace mojo
