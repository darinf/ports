// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PORTS_MOJO_SYSTEM_NODE_CHANNEL_H_
#define PORTS_MOJO_SYSTEM_NODE_CHANNEL_H_

#include "base/macros.h"
#include "base/memory/ref_counted.h"
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
  class Delegate {
   public:
    virtual ~Delegate() {}
    virtual void OnAcceptChild(const ports::NodeName& from_node,
                               const ports::NodeName& parent_name,
                               const ports::NodeName& token) = 0;
    virtual void OnAcceptParent(const ports::NodeName& from_node,
                                const ports::NodeName& token,
                                const ports::NodeName& child_name) = 0;
    virtual void OnEvent(const ports::NodeName& from_node,
                         ports::Event event) = 0;
    virtual void OnConnectToPort(const ports::NodeName& from_node,
                                 const ports::PortName& connector_port,
                                 const std::string& token) = 0;
    virtual void OnConnectToPortAck(const ports::NodeName& from_node,
                                    const ports::PortName& connector_port,
                                    const ports::PortName& connectee_port) = 0;
    virtual void OnRequestIntroduction(const ports::NodeName& from_node,
                                       const ports::NodeName& name) = 0;
    virtual void OnIntroduce(const ports::NodeName& from_name,
                             const ports::NodeName& name,
                             ScopedPlatformHandle channel_handle) = 0;

    virtual void OnChannelError(const ports::NodeName& node) = 0;
  };

  NodeChannel(Delegate* delegate,
              ScopedPlatformHandle platform_handle,
              scoped_refptr<base::TaskRunner> io_task_runner);
  ~NodeChannel() override;

  // Start receiving messages.
  void Start();

  // Used for context in Delegate calls (via |from_node| arguments.)
  void SetRemoteNodeName(const ports::NodeName& name);

  void AcceptChild(const ports::NodeName& parent_name,
                   const ports::NodeName& token);
  void AcceptParent(const ports::NodeName& token,
                    const ports::NodeName& child_name);
  void Event(ports::Event event);
  void ConnectToPort(const std::string& token,
                     const ports::PortName& connector_port);
  void ConnectToPortAck(const ports::PortName& connector_port,
                        const ports::PortName& connectee_port);
  void RequestIntroduction(const ports::NodeName& name);
  void Introduce(const ports::NodeName& name, ScopedPlatformHandle handle);

 private:
  // Channel::Delegate:
  void OnChannelMessage(const void* payload,
                        size_t payload_size,
                        ScopedPlatformHandleVectorPtr handles) override;
  void OnChannelError() override;

  Delegate* delegate_;

  base::Lock name_lock_;
  ports::NodeName remote_node_name_;

  scoped_refptr<Channel> channel_;

  DISALLOW_COPY_AND_ASSIGN(NodeChannel);
};

}  // namespace edk
}  // namespace mojo

#endif  // PORTS_MOJO_SYSTEM_NODE_CHANNEL_H_
