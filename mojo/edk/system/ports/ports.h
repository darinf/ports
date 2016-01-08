// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_EDK_SYSTEM_PORTS_PORTS_H_
#define MOJO_EDK_SYSTEM_PORTS_PORTS_H_

#include <stddef.h>
#include <stdint.h>

#include <functional>
#include <memory>
#include <ostream>

#include "mojo/edk/system/ports/message.h"
#include "mojo/edk/system/ports/name.h"

namespace mojo {
namespace edk {
namespace ports {

enum {
  OK = 0,
  ERROR_PORT_UNKNOWN = -10,
  ERROR_PORT_EXISTS = -11,
  ERROR_PORT_ALREADY_INITIALIZED = -12,
  ERROR_PORT_STATE_UNEXPECTED = -13,
  ERROR_PORT_CANNOT_SEND_SELF = -14,
  ERROR_PORT_PEER_CLOSED = -15,
  ERROR_NOT_IMPLEMENTED = -100,
};

class Port;  // Private to the implementation.

class PortRef {
 public:
  ~PortRef();
  PortRef();
  PortRef(const PortName& name, std::shared_ptr<Port> port);

  PortRef(const PortRef& other);
  PortRef& operator=(const PortRef& other);

  const PortName& name() const { return name_; }

  Port* port() const { return port_.get(); }

 private:
  PortName name_;
  std::shared_ptr<Port> port_;
};

struct PortStatus {
  bool has_messages;
  bool peer_closed;
};

class UserData {
 public:
  virtual ~UserData() {}
};

// Implemented by the embedder.
class NodeDelegate {
 public:
  virtual ~NodeDelegate() {}

  // Port names should be difficult to guess.
  virtual void GenerateRandomPortName(PortName* port_name) = 0;

  // Allocate a message, including a header that can be used by the Node
  // implementation. |num_header_bytes| will be aligned. |num_payload_bytes|
  // may not be aligned. The newly allocated memory need not be zero-filled.
  virtual void AllocMessage(size_t num_header_bytes,
                            size_t num_payload_bytes,
                            size_t num_ports_bytes,
                            ScopedMessage* message) = 0;

  // Forward a message asynchronously to the specified node. This method MUST
  // NOT synchronously call any methods on Node.
  virtual void ForwardMessage(const NodeName& node, ScopedMessage message) = 0;

  // Indicates that the port's status has changed recently. Use Node::GetStatus
  // to query the latest status of the port. Note, this event could be spurious
  // if another thread is simultaneously modifying the status of the port.
  virtual void PortStatusChanged(const PortRef& port_ref) = 0;
};

class Node {
 public:
  // Does not take ownership of the delegate.
  Node(const NodeName& name, NodeDelegate* delegate);
  ~Node();

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
  int SetUserData(const PortRef& port_ref, std::shared_ptr<UserData> user_data);
  int GetUserData(const PortRef& port_ref,
                  std::shared_ptr<UserData>* user_data);

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

  // Allocate a message that can be passed to SendMessage. The caller may
  // mutate the payload and ports arrays before passing the message to
  // SendMessage. The header array should not be modified by the caller.
  int AllocMessage(size_t num_payload_bytes,
                   size_t num_ports,
                   ScopedMessage* message);

  // Sends a message from the specified port to its peer.
  int SendMessage(const PortRef& port_ref, ScopedMessage message);

  // Corresponding to NodeDelegate::ForwardMessage.
  int AcceptMessage(ScopedMessage message);

  // Called to inform this node that communication with another node is lost
  // indefinitely. This triggers cleanup of ports bound to this node.
  int LostConnectionToNode(const NodeName& node);

 private:
  class Impl;
  Impl* impl_;
};

}  // namespace ports
}  // namespace edk
}  // namespace mojo

#endif  // MOJO_EDK_SYSTEM_PORTS_PORTS_H_
