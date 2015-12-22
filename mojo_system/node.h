// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PORTS_MOJO_SYSTEM_NODE_H_
#define PORTS_MOJO_SYSTEM_NODE_H_

#include <unordered_map>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/synchronization/lock.h"
#include "base/task_runner.h"
#include "mojo/edk/embedder/scoped_platform_handle.h"
#include "ports/include/ports.h"
#include "ports/mojo_system/node_channel.h"
#include "ports/mojo_system/node_controller.h"
#include "ports/src/hash_functions.h"

namespace mojo {
namespace edk {

class Node : public ports::NodeDelegate, public NodeChannel::Delegate {
 public:
  class PortObserver {
   public:
    virtual ~PortObserver() {}

    // Notifies the observer that a message is available on a port.
    virtual void OnMessageAvailable(const ports::PortName& name,
                                    ports::ScopedMessage message) = 0;

    // Notifies the observer that a port has been closed. Note that the
    // the observer is automatically removed from the port in this case.
    virtual void OnClosed(const ports::PortName& name) = 0;
  };

  Node();
  ~Node() override;

  const ports::NodeName& name() const { return name_; }

  void set_controller(scoped_ptr<NodeController> controller) {
    controller_ = std::move(controller);
  }

  NodeController* controller() const { return controller_.get(); }

  // Connects this node to a via an OS pipe under |platform_handle|.
  // If |peer_name| is unknown, it should be set to |ports::kInvalidNodeName|.
  void ConnectToPeer(const ports::NodeName& peer_name,
                     ScopedPlatformHandle platform_handle,
                     const scoped_refptr<base::TaskRunner>& io_task_runner);

  // Registers a node named |name| with the given |channel|. |name| must be
  // a valid node name.
  void AddPeer(const ports::NodeName& name, scoped_ptr<NodeChannel> channel);

  // Drops the connection to peer named |name| if one exists.
  void DropPeer(const ports::NodeName& name);

  // Creates a new pair of local ports on this node, returning their names.
  void CreatePortPair(ports::PortName* port0, ports::PortName* port1);

  // Sets a port's observer.
  void SetPortObserver(const ports::PortName& port_name,
                       PortObserver* observer);

  // Sends a message on a port to its peer.
  void SendMessage(const ports::PortName& port_name,
                   ports::ScopedMessage message);

  // Closes a port.
  void ClosePort(const ports::PortName& port_name);

 private:
  // ports::NodeDelegate:
  void GenerateRandomPortName(ports::PortName* port_name) override;
  void SendEvent(const ports::NodeName& node, ports::Event event) override;
  void MessagesAvailable(const ports::PortName& port) override;

  // NodeChannel::Delegate:
  void OnMessageReceived(const ports::NodeName& from_node,
                         NodeChannel::IncomingMessagePtr message) override;
  void OnChannelError(const ports::NodeName& from_node) override;

  ports::NodeName name_;
  scoped_ptr<ports::Node> node_;

  scoped_ptr<NodeController> controller_;

  base::Lock peers_lock_;
  std::unordered_map<ports::NodeName, scoped_ptr<NodeChannel>> peers_;

  base::Lock port_observers_lock_;
  std::unordered_map<ports::PortName, PortObserver*> port_observers_;

  DISALLOW_COPY_AND_ASSIGN(Node);
};

}  // namespace edk
}  // namespace mojo

#endif  // PORTS_MOJO_SYSTEM_NODE_H_
