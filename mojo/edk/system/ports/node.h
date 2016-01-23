// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_EDK_SYSTEM_PORTS_NODE_H_
#define MOJO_EDK_SYSTEM_PORTS_NODE_H_

#include <stddef.h>
#include <stdint.h>

#include <queue>
#include <unordered_map>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/synchronization/lock.h"
#include "mojo/edk/system/ports/event.h"
#include "mojo/edk/system/ports/hash_functions.h"
#include "mojo/edk/system/ports/message.h"
#include "mojo/edk/system/ports/name.h"
#include "mojo/edk/system/ports/port.h"
#include "mojo/edk/system/ports/port_ref.h"
#include "mojo/edk/system/ports/user_data.h"

#undef SendMessage  // Gah, windows

namespace mojo {
namespace edk {
namespace ports {

enum : int {
  OK = 0,
  ERROR_PORT_UNKNOWN = -10,
  ERROR_PORT_EXISTS = -11,
  ERROR_PORT_STATE_UNEXPECTED = -12,
  ERROR_PORT_CANNOT_SEND_SELF = -13,
  ERROR_PORT_PEER_CLOSED = -14,
  ERROR_PORT_CANNOT_SEND_PEER = -15,
  ERROR_NOT_IMPLEMENTED = -100,
};

struct PortStatus {
  bool has_messages;
  bool peer_closed;
};

class NodeDelegate;

class Node {
 public:
  // Does not take ownership of the delegate.
  Node(const NodeName& name, NodeDelegate* delegate);
  ~Node();

  // Returns true if there are no open ports or ports in the process of being
  // transferred from this node to another. If this returns false, then to
  // ensure clean shutdown, it is necessary to keep the node alive and continue
  // routing messages to it via AcceptMessage. This method may be called again
  // after AcceptMessage to check if the Node is now ready to be destroyed.
  bool CanShutdownCleanly();

  // Lookup the named port.
  int GetPort(const PortName& port_name, PortRef* port_ref);

  // Creates a port on this node. Before the port can be used, it must be
  // initialized using InitializePort. This method is useful for bootstrapping
  // a connection between two nodes. Generally, ports are created using
  // CreatePortPair instead.
  int CreateUninitializedPort(PortRef* port_ref);

  // Initializes a newly created port.
  int InitializePort(const PortRef& port_ref,
                     const NodeName& peer_node_name,
                     const PortName& peer_port_name);

  // Generates a new connected pair of ports bound to this node. These ports
  // are initialized and ready to go.
  int CreatePortPair(PortRef* port0_ref, PortRef* port1_ref);

  // User data associated with the port.
  int SetUserData(const PortRef& port_ref,
                  const scoped_refptr<UserData>& user_data);
  int GetUserData(const PortRef& port_ref,
                  scoped_refptr<UserData>* user_data);

  // Prevents further messages from being sent from this port or delivered to
  // this port. The port is removed, and the port's peer is notified of the
  // closure after it has consumed all pending messages.
  int ClosePort(const PortRef& port_ref);

  // Returns the current status of the port.
  int GetStatus(const PortRef& port_ref, PortStatus* port_status);

  // Returns the next available message on the specified port or returns a null
  // message if there are none available. Returns ERROR_PORT_PEER_CLOSED to
  // indicate that this port's peer has closed. In such cases GetMessage may
  // be called until it yields a null message, indicating that no more messages
  // may be read from the port.
  int GetMessage(const PortRef& port_ref, ScopedMessage* message);

  // Like GetMessage, but the caller may optionally supply a selector function
  // that decides whether or not to return the message. If |selector| is a
  // nullptr, then GetMessageIf acts just like GetMessage. The |selector| may
  // not call any Node methods.
  int GetMessageIf(const PortRef& port_ref,
                   std::function<bool(const Message&)> selector,
                   ScopedMessage* message);

  // Sends a message from the specified port to its peer. Note that the message
  // notification may arrive synchronously (via PortStatusChanged() on the
  // delegate) if the peer is local to this Node.
  //
  // If send fails for any reason, |message| is left unchanged. On success,
  // ownserhip is transferred and |message| is reset.
  int SendMessage(const PortRef& port_ref, ScopedMessage* message);

  // Corresponding to NodeDelegate::ForwardMessage.
  int AcceptMessage(ScopedMessage message);

  // Called to inform this node that communication with another node is lost
  // indefinitely. This triggers cleanup of ports bound to this node.
  int LostConnectionToNode(const NodeName& node_name);

 private:
  int OnUserMessage(ScopedMessage message);
  int OnPortAccepted(const PortName& port_name);
  int OnObserveProxy(const PortName& port_name,
                     const ObserveProxyEventData& event);
  int OnObserveProxyAck(const PortName& port_name, uint64_t last_sequence_num);
  int OnObserveClosure(const PortName& port_name, uint64_t last_sequence_num);

  int AddPortWithName(const PortName& port_name,
                      const scoped_refptr<Port>& port);
  void ErasePort(const PortName& port_name);
  void ErasePort_Locked(const PortName& port_name);
  scoped_refptr<Port> GetPort(const PortName& port_name);
  scoped_refptr<Port> GetPort_Locked(const PortName& port_name);

  void WillSendPort_Locked(Port* port,
                           const NodeName& to_node_name,
                           PortName* port_name,
                           PortDescriptor* port_descriptor);
  int AcceptPort(const PortName& port_name,
                 const PortDescriptor& port_descriptor);

  int WillSendMessage_Locked(Port* port,
                             const PortName& port_name,
                             Message* message);
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

  const NodeName name_;
  NodeDelegate* const delegate_;

  // Guards |ports_| as well as any operation which needs to hold multiple port
  // locks simultaneously. Usage of this is subtle: it must NEVER be acquired
  // after a Port lock is acquired, and it must ALWAYS be acquired before
  // calling WillSendMessage_Locked or ForwardMessages_Locked.
  base::Lock ports_lock_;
  std::unordered_map<PortName, scoped_refptr<Port>> ports_;

  DISALLOW_COPY_AND_ASSIGN(Node);
};

}  // namespace ports
}  // namespace edk
}  // namespace mojo

#endif  // MOJO_EDK_SYSTEM_PORTS_NODE_H_
