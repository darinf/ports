// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PORTS_MOJO_SYSTEM_NODE_CHANNEL_H_
#define PORTS_MOJO_SYSTEM_NODE_CHANNEL_H_

#include <stdint.h>

#include <deque>
#include <ostream>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/synchronization/lock.h"
#include "base/task_runner.h"
#include "mojo/edk/embedder/scoped_platform_handle.h"
#include "mojo/edk/embedder/platform_handle_vector.h"
#include "ports/include/ports.h"
#include "ports/mojo_system/channel.h"

namespace mojo {
namespace edk {

// Wraps a Channel to send and receive Node control messages.
class NodeChannel : public Channel::Delegate {
 public:
  class IncomingMessage;
  using IncomingMessagePtr = scoped_ptr<IncomingMessage>;

  class OutgoingMessage;
  using OutgoingMessagePtr = scoped_ptr<OutgoingMessage>;

  enum class MessageType : uint32_t {
    // Sent from the parent node to any child on child startup. Must be the
    // first message received by a child node.
    HELLO_CHILD = 0,

    // Sent from the child to the parent to complete the handshake.
    HELLO_PARENT = 1,

    // Encodes a ports::Event from one node to another.
    EVENT = 2,

    // Sent from one node to another to create a new entangled port pair
    // between them.
    CREATE_PORT = 3,

    // Reply to CREATE_PORT with the receiver's new port name.
    CREATE_PORT_ACK = 4,
  };

  struct MessageHeader {
    MessageType type;
    uint32_t padding;
  };
  static_assert(sizeof(MessageHeader) % kChannelMessageAlignment == 0,
      "MessageHeader must be aligned to kChannelMessageAlignment bytes.");

  struct HelloChildMessageData {
    ports::NodeName parent_name;
    ports::NodeName token_name;
  };

  struct HelloParentMessageData {
    ports::NodeName token_name;
    ports::NodeName child_name;
  };

  struct EventMessageData {
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

  struct CreatePortMessageData {
    ports::PortName initiator_port_name;
  };

  struct CreatePortAckMessageData {
    ports::PortName initiator_port_name;
    ports::PortName entangled_port_name;
  };

  class IncomingMessage {
   public:
    // Copies bytes and takes handles from |message|
    IncomingMessage(Channel::IncomingMessage* message);
    ~IncomingMessage();

    MessageType type() const { return header()->type; }

    template <typename T>
    const T& payload() const {
      DCHECK(data_.size() >= sizeof(MessageHeader) + sizeof(T));
      return *reinterpret_cast<const T*>(&header()[1]);
    }

    size_t payload_size() const { return data_.size(); }

    ScopedPlatformHandleVectorPtr TakeHandles() { return std::move(handles_); }

   private:
    const MessageHeader* header() const {
      return reinterpret_cast<const MessageHeader*>(data_.data());
    }

    std::vector<char> data_;
    ScopedPlatformHandleVectorPtr handles_;

    DISALLOW_COPY_AND_ASSIGN(IncomingMessage);
  };

  class OutgoingMessage {
   public:
    // Allocates an outgoing message which holds |num_bytes| bytes and
    // takes ownership of |handles|.
    OutgoingMessage(MessageType type,
                    size_t payload_size,
                    ScopedPlatformHandleVectorPtr handles);
    ~OutgoingMessage();

    MessageHeader* header() {
      return static_cast<MessageHeader*>(message_->mutable_payload());
    }

    template <typename T>
    T* payload() { return reinterpret_cast<T*>(&header()[1]); }

    Channel::OutgoingMessagePtr TakeMessage() { return std::move(message_); }

   private:
    Channel::OutgoingMessagePtr message_;

    DISALLOW_COPY_AND_ASSIGN(OutgoingMessage);
  };

  class Delegate {
   public:
    virtual ~Delegate() {}
    virtual void OnMessageReceived(const ports::NodeName& node,
                                   IncomingMessagePtr message) = 0;
    virtual void OnChannelError(const ports::NodeName& node) = 0;
  };

  NodeChannel(Delegate* delegate,
              ScopedPlatformHandle platform_handle,
              scoped_refptr<base::TaskRunner> io_task_runner);
  ~NodeChannel() override;

  static OutgoingMessagePtr NewHelloChildMessage(
      const ports::NodeName& parent_name,
      const ports::NodeName& token_name);
  static OutgoingMessagePtr NewHelloParentMessage(
      const ports::NodeName& token_name,
      const ports::NodeName& child_name);
  static OutgoingMessagePtr NewEventMessage(ports::Event event);
  static OutgoingMessagePtr NewCreatePortMessage(
      const ports::PortName& initiator_port_name);
  static OutgoingMessagePtr NewCreatePortAckMessage(
      const ports::PortName& initiator_port_name,
      const ports::PortName& entangled_port_name);

  // Start receiving messages.
  void Start();

  // Used for context in delegate calls since delegates may be watching
  // multiple NodeChannels.
  void SetRemoteNodeName(const ports::NodeName& name);

  void SendMessage(OutgoingMessagePtr message);

 private:
  // Channel::Delegate:
  void OnChannelRead(Channel::IncomingMessage* message) override;
  void OnChannelError() override;

  Delegate* delegate_;

  base::Lock name_lock_;
  ports::NodeName remote_node_name_;

  scoped_refptr<Channel> channel_;

  DISALLOW_COPY_AND_ASSIGN(NodeChannel);
};

std::ostream& operator<<(std::ostream& stream,
                         NodeChannel::MessageType message_type);

}  // namespace edk
}  // namespace mojo

#endif  // PORTS_MOJO_SYSTEM_NODE_CHANNEL_H_
