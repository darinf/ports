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

const size_t kMaxMessageSize = 256 * 1024 * 1024;

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
NodeChannel::MessagePtr NodeChannel::Message::NewInitializeChildMessage(
    const ports::NodeName& parent_name,
    const ports::NodeName& child_name) {
  MessagePtr message(new Message(Type::INITIALIZE_CHILD,
                                 sizeof(InitializeChildData), nullptr));
  InitializeChildData* data =
      reinterpret_cast<InitializeChildData*>(message->payload());
  data->parent_name = parent_name;
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

const NodeChannel::Message::InitializeChildData&
NodeChannel::Message::AsInitializeChild() const {
  DCHECK(header()->type == Type::INITIALIZE_CHILD);
  return *reinterpret_cast<const InitializeChildData*>(payload());
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
  remote_node_name_ = name;
}

void NodeChannel::SendMessage(MessagePtr node_message) {
  Channel::OutgoingMessagePtr message(new Channel::OutgoingMessage(
      node_message->TakeData(), node_message->TakeHandles()));
  channel_->Write(std::move(message));
}

void NodeChannel::OnChannelRead(Channel::IncomingMessage* message) {
  std::vector<char> bytes(message->num_bytes());
  memcpy(bytes.data(), message->data(), message->num_bytes());
  ScopedPlatformHandleVectorPtr handles = message->TakeHandles();

  DLOG(INFO) << "Channel to " << remote_node_name_ << " received message with "
      << bytes.size() << " bytes and " << (handles ? handles->size() : 0)
      << " handles.";

  base::AutoLock lock(read_lock_);
  incoming_data_.emplace_back(std::move(bytes));
  if (handles) {
    for (auto handle : *handles)
      incoming_handles_.push_back(ScopedPlatformHandle(handle));
  }

  // Flush out any complete messages.
  do {
    std::vector<char> front_data = std::move(incoming_data_.front());
    incoming_data_.pop_front();

    // Collapse as many messages as necessary to ensure we at least have a
    // complete contiguous header in the first slot. This shouldn't happen often
    // in practice, if at all.
    while (front_data.size() < sizeof(Message::Header) &&
           !incoming_data_.empty()) {
      size_t front_size = front_data.size();
      std::vector<char> next_data = std::move(incoming_data_.front());
      incoming_data_.pop_front();
      front_data.resize(front_data.size() + next_data.size());
      std::copy(next_data.begin(), next_data.end(),
          front_data.begin() + front_size);
    }

    // If we still don't have a header, push back the front data and exit.
    if (front_data.size() < sizeof(Message::Header)) {
      incoming_data_.emplace_front(std::move(front_data));
      return;
    }

    Message::Header* header =
        reinterpret_cast<Message::Header*>(front_data.data());
    if (header->num_bytes > kMaxMessageSize) {
      // Kill the channel if they try to exceed the max message length.
      channel_ = nullptr;
      return;
    }

    if (header->num_handles > incoming_handles_.size()) {
      // If we don't have enough handles to fulfill the message requirements,
      // push the data back and exit.
      incoming_data_.emplace_front(std::move(front_data));
      return;
    }

    size_t required_bytes = header->num_bytes + sizeof(Message::Header);
    if (front_data.size() > required_bytes) {
      // If we took too much, put some back!
      std::vector<char> remainder(front_data.size() - required_bytes);
      std::copy(front_data.begin() + required_bytes, front_data.end(),
            remainder.begin());
      front_data.resize(required_bytes);
      incoming_data_.emplace_front(std::move(remainder));
    }

    // Collapse data into this buffer until we've accumulated enough bytes for
    // the full message.
    while (front_data.size() < required_bytes && !incoming_data_.empty()) {
      std::vector<char> next_data = std::move(incoming_data_.front());
      incoming_data_.pop_front();

      size_t bytes_to_consume = std::min(required_bytes - front_data.size(),
                                         next_data.size());
      size_t front_size = front_data.size();
      std::copy(next_data.begin(), next_data.begin() + bytes_to_consume,
          front_data.begin() + front_size);
      if (bytes_to_consume < next_data.size()) {
        // We didn't consume all the data, so put some of it back.
        std::vector<char> remainder(next_data.size() - bytes_to_consume);
        std::copy(next_data.begin() + bytes_to_consume, next_data.end(),
            remainder.begin());
        incoming_data_.emplace_front(std::move(remainder));
      }
    }

    if (front_data.size() < required_bytes) {
      // Still not enough data. Put it back and exit.
      incoming_data_.emplace_front(std::move(front_data));
      return;
    }

    // We should have exactly the right amount of data if we're here.
    DCHECK_EQ(front_data.size(), required_bytes);

    ScopedPlatformHandleVectorPtr handles(
        new PlatformHandleVector(header->num_handles));
    for (size_t i = 0; i < handles->size(); ++i) {
      (*handles)[i] = incoming_handles_.front().release();
      incoming_handles_.pop_front();
    }

    DLOG(INFO) << "Dispatching message with " << front_data.size()
        << " bytes and " << handles->size() << " handles.";

    MessagePtr message(
        new Message(std::move(front_data), std::move(handles)));
    delegate_->OnMessageReceived(remote_node_name_, std::move(message));

    // There may be data left after this dispatch. Try to dispatch another.
  } while (!incoming_data_.empty());
}

void NodeChannel::OnChannelError() {
  delegate_->OnChannelError(remote_node_name_);
}

}  // namespace edk
}  // namespace mojo
