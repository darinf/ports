// Copyright 2015, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef PORTS_SRC_NODE_IMPL_H_
#define PORTS_SRC_NODE_IMPL_H_

#include <mutex>
#include <unordered_map>

#include "event.h"
#include "ports/include/ports.h"
#include "hash_functions.h"
#include "port.h"

namespace ports {

class Node::Impl {
 public:
  Impl(const NodeName& name, NodeDelegate* delegate);
  ~Impl();

  int GetPort(const PortName& port_name, PortRef* port_ref);
  int CreatePort(PortRef* port_ref);
  int InitializePort(const PortRef& port_ref,
                     const NodeName& peer_node_name,
                     const PortName& peer_port_name);
  int CreatePortPair(PortRef* port0_ref, PortRef* port1_ref);
  int SetUserData(const PortRef& port_ref,
                  std::shared_ptr<UserData> user_data);
  int ClosePort(const PortRef& port_ref);
  int GetMessage(const PortRef& port_ref, ScopedMessage* message);
  int GetMessageIf(const PortRef& port_ref,
                   MessageSelector* selector,
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
  void ClosePort_Locked(Port* port, const PortName& port_name);

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

#endif  // PORTS_SRC_NODE_IMPL_H_
