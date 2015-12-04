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
  ERROR_PORT_UNKNOWN = -1,
  ERROR_PORT_EXISTS = -2,
  ERROR_PORT_ALREADY_INITIALIZED = -3,
  ERROR_PORT_STATE_UNEXPECTED = -4,
  ERROR_PORT_CANNOT_SEND_SELF = -5,
  ERROR_PORT_PEER_CLOSED = -6,
  ERROR_NOT_IMPLEMENTED = -100,
};

// TODO: Make these 128-bit to reduce guessability.

// Port names are NOT globally unique. They are relative to the node they are
// bound to.
struct PortName {
  PortName() : value(0) {}
  explicit PortName(uint64_t value) : value(value) {}
  bool operator==(const PortName& other) const { return other.value == value; }
  uint64_t value;
};

// Node names are globally unique.
struct NodeName {
  NodeName() : value(0) {}
  explicit NodeName(uint64_t value) : value(value) {}
  bool operator==(const NodeName& other) const { return other.value == value; }
  uint64_t value;
};

struct PortDescriptor {
  PortName name;

  // The following fields are used by the implementation and should not be set
  // before calling SendMessage.
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
  } type;
  PortName port_name;
  NodeName proxy_node_name;
  PortName proxy_port_name;
  NodeName proxy_to_node_name;
  PortName proxy_to_port_name;
  ScopedMessage message;
  uint32_t last_sequence_num;

  explicit Event(Type type) : type(type), last_sequence_num(0) {}
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

  // Closes any ports bound to this node.
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
  // this port. The port is removed, and the port's peer is notified.
  int ClosePort(PortName port);

  // Returns the next available message on the specified port or returns a null
  // message if there are none available.
  int GetMessage(PortName port, ScopedMessage* message);

  // Sends a message from the specified port to its peer.
  int SendMessage(PortName port, ScopedMessage message);

  // Corresponding to NodeDelegate::SendEvent.
  int AcceptEvent(Event event);

 private:
  struct Impl;
  Impl* impl_;
};

}  // namespace ports

#endif  // PORTS_INCLUDE_PORTS_H_
