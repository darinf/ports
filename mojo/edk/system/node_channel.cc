// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/edk/system/node_channel.h"

#include <cstring>
#include <limits>
#include <sstream>

#include "base/logging.h"
#include "base/rand_util.h"
#include "mojo/edk/system/channel.h"

namespace mojo {
namespace edk {

namespace {

template <typename T>
T Align(T t) {
  const auto k = kChannelMessageAlignment;
  return t + (k - (t % k)) % k;
}

enum class MessageType : uint32_t {
  ACCEPT_CHILD,
  ACCEPT_PARENT,
  PORTS_MESSAGE,
  REQUEST_PORT_CONNECTION,
  CONNECT_TO_PORT,
  REQUEST_INTRODUCTION,
  INTRODUCE,
#if defined(OS_WIN)
  RELAY_PORTS_MESSAGE,
  RELAY_PORTS_MESSAGE_ACK,
#endif
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

// This is followed by arbitrary payload data which is interpreted as a token
// string for port location.
struct RequestPortConnectionData {
  ports::PortName connector_port_name;
};

struct ConnectToPortData {
  ports::PortName connector_port_name;
  ports::PortName connectee_port_name;
};

// Used for both REQUEST_INTRODUCTION and INTRODUCE.
//
// For INTRODUCE the message must also include a platform handle the recipient
// can use to communicate with the named node. If said handle is omitted, the
// peer cannot be introduced.
struct IntroductionData {
  ports::NodeName name;
};

#if defined(OS_WIN)
// This struct is followed by an array of handles and then the message data.
struct RelayPortsMessageData {
  ports::NodeName destination;
  uint32_t identifier;
  uint32_t handles_num_bytes;
};

struct RelayPortsMessageAckData {
  uint32_t identifier;
  uint32_t padding;
};
#endif

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
scoped_refptr<NodeChannel> NodeChannel::Create(
    Delegate* delegate,
    ScopedPlatformHandle platform_handle,
    scoped_refptr<base::TaskRunner> io_task_runner) {
  return new NodeChannel(delegate, std::move(platform_handle), io_task_runner);
}

// static
Channel::MessagePtr NodeChannel::CreatePortsMessage(
    size_t payload_size,
    void** payload,
    ScopedPlatformHandleVectorPtr platform_handles) {
  return CreateMessage(MessageType::PORTS_MESSAGE, payload_size,
                       std::move(platform_handles), payload);
}

// static
void NodeChannel::GetPortsMessageData(Channel::Message* message, void** data,
                                      size_t* num_data_bytes) {
  *data = reinterpret_cast<Header*>(message->mutable_payload()) + 1;
  *num_data_bytes = message->payload_size() - sizeof(Header);
}

void NodeChannel::Start() {
  base::AutoLock lock(channel_lock_);
  DCHECK(channel_);
  channel_->Start();
}

void NodeChannel::ShutDown() {
  base::AutoLock lock(channel_lock_);
  if (channel_) {
    channel_->ShutDown();
    channel_ = nullptr;
  }
}

void NodeChannel::SetRemoteProcessHandle(base::ProcessHandle process_handle) {
#if defined(OS_WIN)
  DCHECK(io_task_runner_->RunsTasksOnCurrentThread());
  remote_process_handle_ = process_handle;
#endif
}

void NodeChannel::SetRemoteNodeName(const ports::NodeName& name) {
  DCHECK(io_task_runner_->RunsTasksOnCurrentThread());
  remote_node_name_ = name;
}

void NodeChannel::AcceptChild(const ports::NodeName& parent_name,
                              const ports::NodeName& token) {
  base::AutoLock lock(channel_lock_);
  if (!channel_) {
    DVLOG(2) << "Not sending AcceptChild on closed Channel.";
    return;
  }

  AcceptChildData* data;
  Channel::MessagePtr message = CreateMessage(
      MessageType::ACCEPT_CHILD, sizeof(AcceptChildData), nullptr, &data);
  data->parent_name = parent_name;
  data->token = token;
  channel_->Write(std::move(message));
}

void NodeChannel::AcceptParent(const ports::NodeName& token,
                               const ports::NodeName& child_name) {
  base::AutoLock lock(channel_lock_);
  if (!channel_) {
    DVLOG(2) << "Not sending AcceptParent on closed Channel.";
    return;
  }

  AcceptParentData* data;
  Channel::MessagePtr message = CreateMessage(
      MessageType::ACCEPT_PARENT, sizeof(AcceptParentData), nullptr, &data);
  data->token = token;
  data->child_name = child_name;
  channel_->Write(std::move(message));
}

void NodeChannel::PortsMessage(Channel::MessagePtr message) {
  base::AutoLock lock(channel_lock_);
  if (!channel_) {
    DVLOG(2) << "Not sending PortsMessage on closed Channel.";
    return;
  }

  channel_->Write(std::move(message));
}

void NodeChannel::RequestPortConnection(
    const ports::PortName& connector_port_name,
    const std::string& token) {
  base::AutoLock lock(channel_lock_);
  if (!channel_) {
    DVLOG(2) << "Not sending RequestPortConnection on closed Channel.";
    return;
  }

  RequestPortConnectionData* data;
  Channel::MessagePtr message = CreateMessage(
      MessageType::REQUEST_PORT_CONNECTION,
      sizeof(RequestPortConnectionData) + token.size(), nullptr, &data);
  data->connector_port_name = connector_port_name;
  memcpy(data + 1, token.data(), token.size());
  channel_->Write(std::move(message));
}

void NodeChannel::ConnectToPort(const ports::PortName& connector_port_name,
                                const ports::PortName& connectee_port_name) {
  base::AutoLock lock(channel_lock_);
  if (!channel_) {
    DVLOG(2) << "Not sending ConnectToPort on closed Channel.";
    return;
  }

  ConnectToPortData* data;
  Channel::MessagePtr message = CreateMessage(
      MessageType::CONNECT_TO_PORT, sizeof(ConnectToPortData), nullptr, &data);
  data->connector_port_name = connector_port_name;
  data->connectee_port_name = connectee_port_name;
  channel_->Write(std::move(message));
}

void NodeChannel::RequestIntroduction(const ports::NodeName& name) {
  base::AutoLock lock(channel_lock_);
  if (!channel_) {
    DVLOG(2) << "Not sending RequestIntroduction on closed Channel.";
    return;
  }

  IntroductionData* data;
  Channel::MessagePtr message = CreateMessage(
      MessageType::REQUEST_INTRODUCTION, sizeof(IntroductionData), nullptr,
      &data);
  data->name = name;
  channel_->Write(std::move(message));
}

void NodeChannel::Introduce(const ports::NodeName& name,
                            ScopedPlatformHandle handle) {
  base::AutoLock lock(channel_lock_);
  if (!channel_) {
    DVLOG(2) << "Not sending Introduce on closed Channel.";
    return;
  }

  IntroductionData* data;
#if defined(OS_WIN)
  size_t message_size = sizeof(IntroductionData) +
      (handle.is_valid() ? sizeof(HANDLE) : 0);
  Channel::MessagePtr message = CreateMessage(
      MessageType::INTRODUCE, message_size, nullptr, &data);
  if (remote_process_handle_ != base::kNullProcessHandle &&
      handle.is_valid()) {
    HANDLE* handles = reinterpret_cast<HANDLE*>(data + 1);
    handles[0] = handle.get().handle;
    BOOL result = DuplicateHandle(base::GetCurrentProcessHandle(),
                                  handles[0], remote_process_handle_,
                                  &handles[0], 0, FALSE,
                                  DUPLICATE_CLOSE_SOURCE |
                                      DUPLICATE_SAME_ACCESS);
    DCHECK(result);
    ignore_result(handle.release());
  }
#else
  ScopedPlatformHandleVectorPtr handles;
  if (handle.is_valid()) {
    handles.reset(new PlatformHandleVector(1));
    handles->at(0) = handle.release();
  }
  Channel::MessagePtr message = CreateMessage(
      MessageType::INTRODUCE, sizeof(IntroductionData), std::move(handles),
      &data);
#endif
  data->name = name;
  channel_->Write(std::move(message));
}

#if defined(OS_WIN)
void NodeChannel::RelayPortsMessage(const ports::NodeName& destination,
                                    Channel::MessagePtr message) {
  DCHECK(message->has_handles());

  uint32_t identifier = static_cast<uint32_t>(
      base::RandGenerator(std::numeric_limits<uint32_t>::max()));

  size_t handles_num_bytes = Align(message->num_handles() * sizeof(HANDLE));
  size_t num_bytes = sizeof(RelayPortsMessageData) + message->data_num_bytes() +
                     handles_num_bytes;

  RelayPortsMessageData* data;
  Channel::MessagePtr relay_message = CreateMessage(
      MessageType::RELAY_PORTS_MESSAGE, num_bytes, nullptr, &data);
  data->destination = destination;
  data->identifier = identifier;
  data->handles_num_bytes = static_cast<uint32_t>(handles_num_bytes);

  ScopedPlatformHandleVectorPtr handles = message->TakeHandles();

  // Copy the handles.
  HANDLE* handles_start =
      reinterpret_cast<HANDLE*>(reinterpret_cast<char*>(data + 1));
  for (size_t i = 0; i < handles->size(); ++i)
    handles_start[i] = handles.get()->at(i).handle;

  // Map the handles to the destination process if we are empowered to do so.
  if (remote_process_handle_ != base::kNullProcessHandle) {
    for (size_t i = 0; i < handles->size(); ++i) {
      BOOL result = DuplicateHandle(base::GetCurrentProcessHandle(),
                                    handles_start[i], remote_process_handle_,
                                    &handles_start[i], 0, FALSE,
                                    DUPLICATE_SAME_ACCESS);
      DCHECK(result);
    }
  }

  if (handles->size() * sizeof(HANDLE) < handles_num_bytes) {
    memset(handles_start + handles->size(), 0,
           handles_num_bytes - handles->size() * sizeof(HANDLE));
  }

  // Copy the message bytes.
  memcpy(reinterpret_cast<char*>(handles_start) + handles_num_bytes,
         message->data(),
         message->data_num_bytes());

  // Hold onto these handles until we receive the RelayPortsMessageAck. This
  // way we avoid closing them at the wrong time or leaking them.
  {
    base::AutoLock lock(pending_handles_lock_);
    pending_handles_.insert(
        std::make_pair(identifier, std::move(handles)));
  }

  channel_->Write(std::move(relay_message));
}
#endif

NodeChannel::NodeChannel(Delegate* delegate,
                         ScopedPlatformHandle platform_handle,
                         scoped_refptr<base::TaskRunner> io_task_runner)
    : delegate_(delegate),
      io_task_runner_(io_task_runner),
      channel_(
          Channel::Create(this, std::move(platform_handle), io_task_runner_)) {
}

NodeChannel::~NodeChannel() {
  ShutDown();
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
      Channel::MessagePtr message(
          new Channel::Message(payload_size, std::move(handles)));
      memcpy(message->mutable_payload(), payload, payload_size);
      delegate_->OnPortsMessage(std::move(message));
      break;
    }

    case MessageType::REQUEST_PORT_CONNECTION: {
      const RequestPortConnectionData* data;
      GetMessagePayload(payload, &data);

      const char* token_data = reinterpret_cast<const char*>(data + 1);
      const size_t token_size = payload_size - sizeof(*data) - sizeof(Header);
      std::string token(token_data, token_size);

      delegate_->OnRequestPortConnection(remote_node_name_,
                                         data->connector_port_name, token);
      break;
    }

    case MessageType::CONNECT_TO_PORT: {
      const ConnectToPortData* data;
      GetMessagePayload(payload, &data);
      delegate_->OnConnectToPort(remote_node_name_, data->connector_port_name,
                                 data->connectee_port_name);
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
#if defined(OS_WIN)
      if (payload_size == sizeof(IntroductionData) + sizeof(Header) +
              sizeof(HANDLE)) {
        const HANDLE* handles = reinterpret_cast<const HANDLE*>(data + 1);
        handle.reset(PlatformHandle(handles[0]));
      }
#else
      if (handles && !handles->empty()) {
        handle = ScopedPlatformHandle(handles->at(0));
        handles->clear();
      }
#endif
      delegate_->OnIntroduce(remote_node_name_, data->name, std::move(handle));
      break;
    }

#if defined(OS_WIN)
    case MessageType::RELAY_PORTS_MESSAGE: {
      OnRelayPortsMessage(payload, payload_size);
      break;
    }

    case MessageType::RELAY_PORTS_MESSAGE_ACK: {
      const RelayPortsMessageAckData* data;
      GetMessagePayload(payload, &data);
      OnRelayPortsMessageAck(data->identifier);
      break;
    }
#endif

    default:
      DLOG(ERROR) << "Received unknown message type "
                  << static_cast<uint32_t>(header->type) << " from node "
                  << remote_node_name_;
      delegate_->OnChannelError(remote_node_name_);
      break;
  }
}

void NodeChannel::OnChannelError() {
  DCHECK(io_task_runner_->RunsTasksOnCurrentThread());

  ShutDown();
  // |OnChannelError()| may cause |this| to be destroyed, but still need access
  // to the name name after that destruction. So may a copy of
  // |remote_node_name_| so it can be used if |this| becomes destroyed.
  ports::NodeName node_name = remote_node_name_;
  delegate_->OnChannelError(node_name);
}

#if defined(OS_WIN)
void NodeChannel::OnRelayPortsMessage(const void* payload,
                                      size_t payload_size) {
  const RelayPortsMessageData* data;
  GetMessagePayload(payload, &data);

  const HANDLE* handles_start =
      reinterpret_cast<const HANDLE*>(reinterpret_cast<const char*>(data + 1));
  const char* message_start =
      reinterpret_cast<const char*>(handles_start) + data->handles_num_bytes;
  const char* message_end = static_cast<const char*>(payload) + payload_size;
  size_t message_size = message_end - message_start;

  // Maybe we should be killing the guy who sent us this message instead.
  CHECK(message_end > message_start);

  Channel::MessagePtr message(
      new Channel::Message(message_start, message_size));

  // Maybe we should be killing the guy who sent us this message instead.
  CHECK(message->num_handles() * sizeof(HANDLE) <= data->handles_num_bytes);

  ScopedPlatformHandleVectorPtr handles(new PlatformHandleVector());
  for (size_t i = 0; i < message->num_handles(); ++i)
    handles.get()->push_back(PlatformHandle(handles_start[i]));
  message->SetHandles(std::move(handles));

  // Send 'ack' message
  RelayPortsMessageAckData* ack_data;
  Channel::MessagePtr ack_message = CreateMessage(
      MessageType::RELAY_PORTS_MESSAGE_ACK, sizeof(*ack_data), nullptr,
      &ack_data);
  ack_data->identifier = data->identifier;
  ack_data->padding = 0;
  channel_->Write(std::move(ack_message));

  delegate_->OnRelayPortsMessage(data->destination, std::move(message));
}

void NodeChannel::OnRelayPortsMessageAck(uint32_t identifier) {
  pending_handles_.erase(identifier);
}

#endif

}  // namespace edk
}  // namespace mojo
