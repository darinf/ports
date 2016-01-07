// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PORTS_INCLUDE_PORTS_H_
#define PORTS_INCLUDE_PORTS_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <ostream>

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

// Port names are globally unique.
struct PortName {
  PortName() : value_major(0), value_minor(0) {}
  PortName(uint64_t value_major, uint64_t value_minor)
      : value_major(value_major), value_minor(value_minor) {}
  uint64_t value_major;
  uint64_t value_minor;
};

const PortName kInvalidPortName = {0, 0};

inline bool operator==(const PortName& a, const PortName& b) {
  return a.value_major == b.value_major && a.value_minor == b.value_minor;
}
inline bool operator!=(const PortName& a, const PortName& b) {
  return !(a == b);
}

std::ostream& operator<<(std::ostream& stream, const PortName& name);

// Node names are globally unique.
struct NodeName {
  NodeName() : value_major(0), value_minor(0) {}
  NodeName(uint64_t value_major, uint64_t value_minor)
      : value_major(value_major), value_minor(value_minor) {}
  uint64_t value_major;
  uint64_t value_minor;
};

const NodeName kInvalidNodeName = {0, 0};

inline bool operator==(const NodeName& a, const NodeName& b) {
  return a.value_major == b.value_major && a.value_minor == b.value_minor;
}
inline bool operator!=(const NodeName& a, const NodeName& b) {
  return !(a == b);
}

std::ostream& operator<<(std::ostream& stream, const NodeName& name);

// This class is designed to be subclassed by the embedder. See NodeDelegate's
// AllocMessage method.
class Message {
 public:
  virtual ~Message() {}

  static void Parse(const void* bytes,
                    size_t num_bytes,
                    size_t* num_header_bytes,
                    size_t* num_payload_bytes,
                    size_t* num_ports_bytes);

  // Header bytes are used by the Node implementation.
  void* mutable_header_bytes() { return start_; }
  const void* header_bytes() const { return start_; }
  size_t num_header_bytes() const { return num_header_bytes_; }

  void* mutable_payload_bytes() {
    return start_ + num_header_bytes_ + num_ports_bytes_;
  }
  const void* payload_bytes() const {
    return const_cast<Message*>(this)->mutable_payload_bytes();
  }
  size_t num_payload_bytes() const { return num_payload_bytes_; }

  PortName* mutable_ports() {
    return reinterpret_cast<PortName*>(start_ + num_header_bytes_);
  }
  const PortName* ports() const {
    return const_cast<Message*>(this)->mutable_ports();
  }
  size_t num_ports_bytes() const { return num_ports_bytes_; }
  size_t num_ports() const { return num_ports_bytes_ / sizeof(PortName); }

 protected:
  Message(size_t num_header_bytes,
          size_t num_payload_bytes,
          size_t num_ports_bytes);
  Message(const Message& other) = delete;
  void operator=(const Message& other) = delete;

  // Note: storage is [header][ports][payload].
  char* start_;
  size_t num_header_bytes_;
  size_t num_ports_bytes_;
  size_t num_payload_bytes_;
};

typedef std::unique_ptr<Message> ScopedMessage;

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

class UserData {
 public:
  virtual ~UserData() {}
};

class MessageSelector {
 public:
  // Returns true to select the given message.
  virtual bool Select(const Message& message) = 0;
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

  // Expected to call Node's GetMessage method to access the next available
  // message. There may be zero or more messages available.
  virtual void MessagesAvailable(const PortRef& port_ref) = 0;
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

  // Returns the next available message on the specified port or returns a null
  // message if there are none available. Returns ERROR_PORT_PEER_CLOSED to
  // indicate that this port's peer has closed, meaning that no further
  // messages will be readable from this port.
  int GetMessage(const PortRef& port_ref, ScopedMessage* message);

  // Like GetMessage, but the caller may optionally supply a MessageSelector
  // that decides whether or not to return the message. If |selector| is null,
  // then GetMessageIf acts just like GetMessage. The |selector| may not call
  // any Node methods.
  int GetMessageIf(const PortRef& port_ref,
                   MessageSelector* selector,
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

#endif  // PORTS_INCLUDE_PORTS_H_
