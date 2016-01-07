// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/edk/system/node_channel.h"

#include <cstring>
#include <limits>
#include <sstream>

#include "base/logging.h"
#include "mojo/edk/system/channel.h"

namespace mojo {
namespace edk {

namespace {

enum class MessageType : uint32_t {
  ACCEPT_CHILD,
  ACCEPT_PARENT,
  PORTS_MESSAGE,
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

// static
Channel::MessagePtr NodeChannel::CreatePortsMessage(
    size_t payload_size,
    void** payload,
    ScopedPlatformHandleVectorPtr platform_handles) {
  return CreateMessage(MessageType::PORTS_MESSAGE, payload_size,
                       std::move(platform_handles), payload);
}

NodeChannel::NodeChannel(Delegate* delegate,
                         ScopedPlatformHandle platform_handle,
                         scoped_refptr<base::TaskRunner> io_task_runner)
    : delegate_(delegate),
      io_task_runner_(io_task_runner),
      channel_(
          Channel::Create(this, std::move(platform_handle), io_task_runner_)) {}

NodeChannel::~NodeChannel() {
  DCHECK(io_task_runner_->RunsTasksOnCurrentThread());
  channel_->ShutDown();
}

void NodeChannel::Start() {
  DCHECK(io_task_runner_->RunsTasksOnCurrentThread());
  channel_->Start();
}

void NodeChannel::SetRemoteNodeName(const ports::NodeName& name) {
  DCHECK(io_task_runner_->RunsTasksOnCurrentThread());
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

void NodeChannel::PortsMessage(Channel::MessagePtr message) {
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
  DCHECK(io_task_runner_->RunsTasksOnCurrentThread());

  const Header* header = static_cast<const Header*>(payload);
  switch (header->type) {
    case MessageType::ACCEPT_CHILD: {
      const AcceptChildData* data;
      GetMessagePayload(payload, &data);
      delegate_->OnAcceptChild(remote_node_name_, data->parent_name,
                               data->token);
      break;
    }

    case MessageType::ACCEPT_PARENT: {
      const AcceptParentData* data;
      GetMessagePayload(payload, &data);
      delegate_->OnAcceptParent(remote_node_name_, data->token,
                                data->child_name);
      break;
    }

    case MessageType::PORTS_MESSAGE: {
      const void* data;
      GetMessagePayload(payload, &data);
      delegate_->OnPortsMessage(remote_node_name_, data,
                                payload_size - sizeof(Header),
                                std::move(handles));
      break;
    }

    case MessageType::CONNECT_TO_PORT: {
      const ConnectToPortData* data;
      GetMessagePayload(payload, &data);
      // TODO: yikes
      std::string token(
          reinterpret_cast<const char*>(&data[1]),
          payload_size - sizeof(Header) - sizeof(ConnectToPortData));
      delegate_->OnConnectToPort(remote_node_name_, data->connector_port,
                                 token);
      break;
    }

    case MessageType::CONNECT_TO_PORT_ACK: {
      const ConnectToPortAckData* data;
      GetMessagePayload(payload, &data);
      delegate_->OnConnectToPortAck(remote_node_name_, data->connector_port,
                                    data->connectee_port);
      break;
    }

    case MessageType::REQUEST_INTRODUCTION: {
      const IntroductionData* data;
      GetMessagePayload(payload, &data);
      delegate_->OnRequestIntroduction(remote_node_name_, data->name);
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
      delegate_->OnIntroduce(remote_node_name_, data->name, std::move(handle));
      break;
    }

    default:
      DLOG(ERROR) << "Received unknown message type "
                  << static_cast<uint32_t>(header->type) << " from node "
                  << remote_node_name_;
      delegate_->OnChannelError(remote_node_name_);
      break;
  }
}

void NodeChannel::OnChannelError() {
  delegate_->OnChannelError(remote_node_name_);
}

}  // namespace edk
}  // namespace mojo
