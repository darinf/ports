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

namespace {

enum class MessageType : uint32_t {
  ACCEPT_CHILD,
  ACCEPT_PARENT,
  EVENT,
  CONNECT_TO_PORT,
  CONNECT_TO_PORT_ACK,
  REQUEST_INTRODUCTION,
  INTRODUCE,
};

struct Header {
  MessageType type;
  uint32_t padding;
};

static_assert(sizeof(Header) % kChannelMessageAlignment == 0,
    "Invalid header size.");

struct AcceptChildData {
  ports::NodeName parent_name;
  ports::NodeName token;
};

struct AcceptParentData {
  ports::NodeName token;
  ports::NodeName child_name;
};

// This data is followed by a serialized ports::Message in the payload.
struct EventData {
  uint32_t type;
  ports::PortName port_name;
  union {
    struct {
      ports::NodeName proxy_node_name;
      ports::PortName proxy_peer_name;
      ports::NodeName proxy_to_node_name;
      ports::PortName proxy_to_peer_name;
    } observe_proxy;
    struct {
      uint32_t last_sequence_num;
    } observe_proxy_ack;
    struct {
      uint32_t last_sequence_num;
    } observe_closure;
  };
};

// This data is followed by arbitrary string contents in the payload, which
// are used as a token to identify the target port.
struct ConnectToPortData {
  ports::PortName connector_port;
};

// Response sent by the connectee, i.e., the receiver of a ConnectToPort
// message. If the connectee rejects the connection request, |connectee_port|
// should be |ports::kInvalidPortName|.
struct ConnectToPortAckData {
  ports::PortName connector_port;
  ports::PortName connectee_port;
};

// Used for both REQUEST_INTRODUCTION and INTRODUCE.
//
// For INTRODUCE the message must also include a platform handle the recipient
// can use to communicate with the named node. If said handle is omitted, the
// peer cannot be introduced.
struct IntroductionData {
  ports::NodeName name;
};

template <typename DataType>
Channel::MessagePtr CreateMessage(MessageType type,
                                  size_t payload_size,
                                  ScopedPlatformHandleVectorPtr handles,
                                  DataType** out_data) {
  Channel::MessagePtr message(
      new Channel::Message(sizeof(Header) + payload_size, std::move(handles)));
  Header* header = reinterpret_cast<Header*>(message->mutable_payload());
  header->type = type;
  header->padding = 0;
  *out_data = reinterpret_cast<DataType*>(&header[1]);
  return message;
};

template <typename DataType>
void GetMessagePayload(const void* bytes, DataType** out_data) {
  *out_data = reinterpret_cast<const DataType*>(
      static_cast<const char*>(bytes) + sizeof(Header));
}

}  // namespace

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

void NodeChannel::AcceptChild(const ports::NodeName& parent_name,
                              const ports::NodeName& token) {
  AcceptChildData* data;
  Channel::MessagePtr message = CreateMessage(
      MessageType::ACCEPT_CHILD, sizeof(AcceptChildData), nullptr, &data);
  data->parent_name = parent_name;
  data->token = token;
  channel_->Write(std::move(message));
}

void NodeChannel::AcceptParent(const ports::NodeName& token,
                               const ports::NodeName& child_name) {
  AcceptParentData* data;
  Channel::MessagePtr message = CreateMessage(
      MessageType::ACCEPT_PARENT, sizeof(AcceptParentData), nullptr, &data);
  data->token = token;
  data->child_name = child_name;
  channel_->Write(std::move(message));
}

void NodeChannel::Event(ports::Event event) {
  size_t event_size = sizeof(EventData);
  size_t message_size = 0;
  if (event.message) {
    DCHECK(event.type == ports::Event::kAcceptMessage);
    message_size = sizeof(ports::Message) + event.message->num_bytes +
        event.message->num_ports * sizeof(ports::PortDescriptor);
    event_size += message_size;
  }

  EventData* data;
  Channel::MessagePtr message = CreateMessage(
      MessageType::EVENT, event_size, nullptr, &data);
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

  channel_->Write(std::move(message));
}

void NodeChannel::ConnectToPort(const std::string& token,
                                const ports::PortName& connector_port) {
  ConnectToPortData* data;
  Channel::MessagePtr message = CreateMessage(
      MessageType::CONNECT_TO_PORT, sizeof(ConnectToPortData) + token.size(),
      nullptr, &data);
  data->connector_port = connector_port;
  memcpy(&data[1], token.data(), token.size());
  channel_->Write(std::move(message));
}

void NodeChannel::ConnectToPortAck(const ports::PortName& connector_port,
                                   const ports::PortName& connectee_port) {
  ConnectToPortAckData* data;
  Channel::MessagePtr message = CreateMessage(
      MessageType::CONNECT_TO_PORT_ACK, sizeof(ConnectToPortAckData), nullptr,
      &data);
  data->connector_port = connector_port,
  data->connectee_port = connectee_port;
  channel_->Write(std::move(message));
}

void NodeChannel::RequestIntroduction(const ports::NodeName& name) {
  IntroductionData* data;
  Channel::MessagePtr message = CreateMessage(
      MessageType::REQUEST_INTRODUCTION, sizeof(IntroductionData), nullptr,
      &data);
  data->name = name;
  channel_->Write(std::move(message));
}

void NodeChannel::Introduce(const ports::NodeName& name,
                            ScopedPlatformHandle handle) {
  ScopedPlatformHandleVectorPtr handles;
  if (handle.is_valid()) {
    handles.reset(new PlatformHandleVector(1));
    handles->at(0) = handle.release();
  }
  IntroductionData* data;
  Channel::MessagePtr message = CreateMessage(
      MessageType::INTRODUCE, sizeof(IntroductionData), std::move(handles),
      &data);
  data->name = name;
  channel_->Write(std::move(message));
}

void NodeChannel::OnChannelMessage(const void* payload,
                                   size_t payload_size,
                                   ScopedPlatformHandleVectorPtr handles) {
  ports::NodeName from_node;
  {
    base::AutoLock lock(name_lock_);
    from_node = remote_node_name_;
  }

  const Header* header = static_cast<const Header*>(payload);
  switch (header->type) {
    case MessageType::ACCEPT_CHILD: {
      const AcceptChildData* data;
      GetMessagePayload(payload, &data);
      delegate_->OnAcceptChild(from_node, data->parent_name, data->token);
      break;
    }

    case MessageType::ACCEPT_PARENT: {
      const AcceptParentData* data;
      GetMessagePayload(payload, &data);
      delegate_->OnAcceptParent(from_node, data->token, data->child_name);
      break;
    }

    case MessageType::EVENT: {
      // TODO: Make this less bad
      const EventData* data;
      GetMessagePayload(payload, &data);
      ports::Event event(static_cast<ports::Event::Type>(data->type));
      event.port_name = data->port_name;
      if (event.type == ports::Event::kAcceptMessage) {
        size_t message_size = payload_size - sizeof(Header) - sizeof(EventData);
        const ports::Message* m =
            reinterpret_cast<const ports::Message*>(&data[1]);
        ports::Message* own_m = ports::AllocMessage(m->num_bytes, m->num_ports);
        memcpy(own_m, m, message_size);
        own_m->ports = reinterpret_cast<ports::PortDescriptor*>(
            reinterpret_cast<char*>(own_m) + sizeof(ports::Message));
        own_m->bytes = reinterpret_cast<char*>(own_m->ports) +
            own_m->num_ports * sizeof(ports::PortDescriptor);
        event.message.reset(own_m);
      }
      memcpy(&event.observe_proxy, &data->observe_proxy,
          sizeof(event.observe_proxy));

      delegate_->OnEvent(from_node, std::move(event));
      break;
    }

    case MessageType::CONNECT_TO_PORT: {
      const ConnectToPortData* data;
      GetMessagePayload(payload, &data);
      // TODO: yikes
      std::string token(
          reinterpret_cast<const char*>(&data[1]),
          payload_size - sizeof(Header) - sizeof(ConnectToPortData));
      delegate_->OnConnectToPort(from_node, data->connector_port, token);
      break;
    }

    case MessageType::CONNECT_TO_PORT_ACK: {
      const ConnectToPortAckData* data;
      GetMessagePayload(payload, &data);
      delegate_->OnConnectToPortAck(
          from_node, data->connector_port, data->connectee_port);
      break;
    }

    case MessageType::REQUEST_INTRODUCTION: {
      const IntroductionData* data;
      GetMessagePayload(payload, &data);
      delegate_->OnRequestIntroduction(from_node, data->name);
      break;
    }

    case MessageType::INTRODUCE: {
      const IntroductionData* data;
      GetMessagePayload(payload, &data);
      ScopedPlatformHandle handle;
      if (handles && !handles->empty()) {
        handle = ScopedPlatformHandle(handles->at(0));
        handles->clear();
      }
      delegate_->OnIntroduce(from_node, data->name, std::move(handle));
      break;
    }

    default:
      DLOG(ERROR) << "Received unknown message type "
                  << static_cast<uint32_t>(header->type) << " from node "
                  << from_node;
      delegate_->OnChannelError(from_node);
      break;
  }
}

void NodeChannel::OnChannelError() {
  ports::NodeName from_node;
  {
    base::AutoLock lock(name_lock_);
    from_node = remote_node_name_;
  }
  delegate_->OnChannelError(from_node);
}

}  // namespace edk
}  // namespace mojo
