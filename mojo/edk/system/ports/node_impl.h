// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_EDK_SYSTEM_PORTS_NODE_IMPL_H_
#define MOJO_EDK_SYSTEM_PORTS_NODE_IMPL_H_

#include <mutex>
#include <unordered_map>

#include "mojo/edk/system/ports/event.h"
#include "mojo/edk/system/ports/hash_functions.h"
#include "mojo/edk/system/ports/port.h"
#include "mojo/edk/system/ports/ports.h"

namespace mojo {
namespace edk {
namespace ports {

class Node::Impl {
 public:
  Impl(const NodeName& name, NodeDelegate* delegate);
  ~Impl();

  int GetPort(const PortName& port_name, PortRef* port_ref);
  int CreateUninitializedPort(PortRef* port_ref);
  int InitializePort(const PortRef& port_ref,
                     const NodeName& peer_node_name,
                     const PortName& peer_port_name);
  int CreatePortPair(PortRef* port0_ref, PortRef* port1_ref);
  int SetUserData(const PortRef& port_ref,
                  std::shared_ptr<UserData> user_data);
  int GetUserData(const PortRef& port_ref,
                  std::shared_ptr<UserData>* user_data);
  int ClosePort(const PortRef& port_ref);
  int GetStatus(const PortRef& port_ref, PortStatus* port_status);
  int GetMessage(const PortRef& port_ref, ScopedMessage* message);
  int GetMessageIf(const PortRef& port_ref,
                   std::function<bool(const Message&)> selector,
                   ScopedMessage* message);
  int AllocMessage(size_t num_payload_bytes,
                   size_t num_ports,
                   ScopedMessage* message);
  int SendMessage(const PortRef& port_ref, ScopedMessage message);
  int AcceptMessage(ScopedMessage message);
  int LostConnectionToNode(const NodeName& node_name);

 private:
  int OnUserMessage(ScopedMessage message);
  int OnPortAccepted(const PortName& port_name);
  int OnObserveProxy(const PortName& port_name,
                     const ObserveProxyEventData& event);
  int OnObserveProxyAck(const PortName& port_name, uint32_t last_sequence_num);
  int OnObserveClosure(const PortName& port_name, uint32_t last_sequence_num);

  int AddPortWithName(const PortName& port_name,
                      const std::shared_ptr<Port>& port);
  void ErasePort(const PortName& port_name);
  std::shared_ptr<Port> GetPort(const PortName& port_name);

  void WillSendPort_Locked(Port* port,
                           const NodeName& to_node_name,
                           PortName* port_name,
                           PortDescriptor* port_descriptor);
  int AcceptPort(const PortName& port_name,
                 const PortDescriptor& port_descriptor);

  int SendMessage_Locked(Port* port,
                         const PortName& port_name,
                         ScopedMessage message);
  int ForwardMessages_Locked(Port* port, const PortName& port_name);
  void InitiateProxyRemoval_Locked(Port* port, const PortName& port_name);
  void MaybeRemoveProxy_Locked(Port* port, const PortName& port_name);

  ScopedMessage NewInternalMessage_Helper(const PortName& port_name,
                                          const EventType& type,
                                          const void* data,
                                          size_t num_data_bytes);

  ScopedMessage NewInternalMessage(const PortName& port_name,
                                   const EventType& type) {
    return NewInternalMessage_Helper(port_name, type, nullptr, 0);
  }

  template <typename EventData>
  ScopedMessage NewInternalMessage(const PortName& port_name,
                                   const EventType& type,
                                   const EventData& data) {
    return NewInternalMessage_Helper(port_name, type, &data, sizeof(data));
  }

  NodeName name_;
  NodeDelegate* delegate_;

  std::mutex ports_lock_;
  std::unordered_map<PortName, std::shared_ptr<Port>> ports_;

  std::mutex send_with_ports_lock_;
};

}  // namespace ports
}  // namespace edk
}  // namespace mojo

#endif  // MOJO_EDK_SYSTEM_PORTS_NODE_IMPL_H_
