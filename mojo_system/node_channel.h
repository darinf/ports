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
  class Message;
  using MessagePtr = scoped_ptr<Message>;

  class Message {
   public:
    enum class Type : uint32_t {
      // Sent from the parent node to any child on child startup. Must be the
      // first message received by a child node.
      HELLO_CHILD = 0,

      // Sent from the child to the parent to complete the handshake.
      HELLO_PARENT = 1,

      // Encodes a ports::Event from one node to another.
      EVENT = 2,
    };

    struct HelloChildData {
      ports::NodeName parent_name;
      ports::NodeName token_name;
    };

    struct HelloParentData {
      ports::NodeName token_name;
      ports::NodeName child_name;
    };

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
      ports::Message* message;
    };

    struct Header {
      uint32_t num_bytes;
      Type type;
      uint32_t num_handles;
      uint32_t padding;
    };

    static_assert(sizeof(Header) % 8 == 0,
        "NodeChannel::Message::Header size must be 8-byte aligned.");

    // Takes ownership of |data| contents and |handles|.
    Message(std::vector<char> data,
            ScopedPlatformHandleVectorPtr handles);
    ~Message();

    Type type() const { return header()->type; }

    static MessagePtr NewHelloChildMessage(
        const ports::NodeName& parent_name,
        const ports::NodeName& token_name);
    static MessagePtr NewHelloParentMessage(
        const ports::NodeName& token_name,
        const ports::NodeName& child_name);
    static MessagePtr NewEventMessage(ports::Event event);

    const HelloChildData& AsHelloChild() const;
    const HelloParentData& AsHelloParent() const;
    const EventData& AsEvent() const;

    const void* data() const { return data_.data(); }
    size_t num_bytes() const { return data_.size(); }
    ScopedPlatformHandleVectorPtr TakeHandles() { return std::move(handles_); }

   private:
    Message(Type type, size_t num_bytes, ScopedPlatformHandleVectorPtr handles);

    Header* header() { return reinterpret_cast<Header*>(data_.data()); }
    const Header* header() const {
      return reinterpret_cast<const Header*>(data_.data());
    }
    void* payload() { return &header()[1]; }
    const void* payload() const { return &header()[1]; }

    std::vector<char> data_;
    ScopedPlatformHandleVectorPtr handles_;

    DISALLOW_COPY_AND_ASSIGN(Message);
  };

  class Delegate {
   public:
    virtual ~Delegate() {}
    virtual void OnMessageReceived(const ports::NodeName& node,
                                   MessagePtr message) = 0;
    virtual void OnChannelError(const ports::NodeName& node) = 0;
  };

  NodeChannel(Delegate* delegate,
              ScopedPlatformHandle platform_handle,
              scoped_refptr<base::TaskRunner> io_task_runner);
  ~NodeChannel() override;

  // Used for context in delegate calls since delegates may be watching
  // multiple NodeChannels.
  void SetRemoteNodeName(const ports::NodeName& name);

  void SendMessage(MessagePtr message);

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
                         NodeChannel::Message::Type message_type);

}  // namespace edk
}  // namespace mojo

#endif  // PORTS_MOJO_SYSTEM_NODE_CHANNEL_H_
