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

#ifndef PORTS_INCLUDE_PORTS_H_
#define PORTS_INCLUDE_PORTS_H_

#include <cstddef>
#include <cstdint>
#include <memory>

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

inline bool operator==(const PortName& a, const PortName& b) {
  return a.value_major == b.value_major && a.value_minor == b.value_minor;
}
inline bool operator!=(const PortName& a, const PortName& b) {
  return !(a == b);
}

// Node names are globally unique.
struct NodeName {
  NodeName() : value_major(0), value_minor(0) {}
  NodeName(uint64_t value_major, uint64_t value_minor)
      : value_major(value_major), value_minor(value_minor) {}
  uint64_t value_major;
  uint64_t value_minor;
};

inline bool operator==(const NodeName& a, const NodeName& b) {
  return a.value_major == b.value_major && a.value_minor == b.value_minor;
}
inline bool operator!=(const NodeName& a, const NodeName& b) {
  return !(a == b);
}

struct PortDescriptor {
  PortName name;

  // The following fields should be ignored by the embedder.
  NodeName peer_node_name;
  PortName peer_port_name;
  NodeName referring_node_name;
  PortName referring_port_name;
  uint32_t next_sequence_num;
};

struct Message {
  uint32_t sequence_num;  // This field should be ignored by the embedder.
  void* bytes;
  size_t num_bytes;
  PortDescriptor* ports;
  size_t num_ports;
};

// Message objects should only be allocated using this function.
Message* AllocMessage(size_t num_bytes, size_t num_ports);

// Message objects should only be freed using this function.
void FreeMessage(Message* message);

struct MessageDeleter {
  void operator()(Message* message) { FreeMessage(message); }
};

typedef std::unique_ptr<Message, MessageDeleter> ScopedMessage;

struct Event {
  enum Type {
    kAcceptMessage,
    kPortAccepted,
    kObserveProxy,
    kObserveProxyAck,
    kObserveClosure,
  } type;
  PortName port_name;
  ScopedMessage message;
  union {
    struct {
      NodeName proxy_node_name;
      PortName proxy_port_name;
      NodeName proxy_to_node_name;
      PortName proxy_to_port_name;
    } observe_proxy;
    struct {
      uint32_t last_sequence_num;
    } observe_proxy_ack;
    struct {
      uint32_t last_sequence_num;
    } observe_closure;
  };
  explicit Event(Type type);
  Event(Event&& other);
  ~Event();

  Event& operator=(Event&& other);
};

// Implemented by the embedder.
class NodeDelegate {
 public:
  // Port names should be difficult to guess.
  virtual PortName GenerateRandomPortName() = 0;

  // Send an event asynchronously to the specified node. This method MUST NOT
  // synchronously call any methods on Node.
  virtual void SendEvent(NodeName node, Event event) = 0;

  // Expected to call Node's GetMessage method to access the next available
  // message. There may be zero or more messages available.
  virtual void MessagesAvailable(PortName port) = 0;
};

class Node {
 public:
  Node(NodeName name, NodeDelegate* delegate);
  ~Node();

  // Creates a port on this node. Before the port can be used, it must be
  // initialized using InitializePort. This method is useful for bootstrapping
  // a connection between two nodes. Generally, ports are created using
  // CreatePortPair instead.
  int CreatePort(PortName* port);

  // Initializes a newly created port.
  int InitializePort(PortName port, NodeName peer_node, PortName peer_port);

  // Generates a new connected pair of ports bound to this node. These ports
  // are initialized and ready to go.
  int CreatePortPair(PortName* port0, PortName* port1);

  // Prevents further messages from being sent from this port or delivered to
  // this port. The port is removed, and the port's peer is notified of the
  // closure after it has consumed all pending messages.
  int ClosePort(PortName port);

  // Returns the next available message on the specified port or returns a null
  // message if there are none available.
  int GetMessage(PortName port, ScopedMessage* message);

  // Sends a message from the specified port to its peer.
  int SendMessage(PortName port, ScopedMessage message);

  // Corresponding to NodeDelegate::SendEvent.
  int AcceptEvent(Event event);

  // Called to inform this node that communication with another node is lost
  // indefinitely. This triggers cleanup of ports bound to this node.
  int LostConnectionToNode(NodeName node);

 private:
  class Impl;
  Impl* impl_;
};

}  // namespace ports

#endif  // PORTS_INCLUDE_PORTS_H_
