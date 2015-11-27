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

#include <stddef.h>
#include <stdint.h>

namespace ports {

typedef uint64_t PortName;
typedef uint64_t NodeName;

enum {
  OK = 0,
  ERROR = -1,
};

struct Message {
  uint32_t sequence_num;
  const void* bytes;
  size_t num_bytes;
  const PortName* dependent_ports;
  size_t num_dependent_ports;
};

Message* AllocMessage(
    size_t num_bytes,
    size_t num_dependent_ports);

void FreeMessage(
    Message* message);

class NodeDelegate {
 public:
  // Send_* methods MUST NOT call back into any Node methods synchronously.

  virtual int Send_AcceptMessage(
      NodeName to_node,
      PortName port,
      Message* message) = 0;

  virtual int Send_AcceptPort(
      NodeName to_node,
      PortName port,
      PortName peer,
      NodeName peer_node,
      uint32_t next_sequence_num) = 0;

  virtual int Send_AcceptPortAck(
      NodeName to_node,
      PortName port) = 0;

  virtual int Send_UpdatePort(
      NodeName to_node,
      PortName port,
      NodeName peer_node) = 0;

  virtual int Send_UpdatePortAck(
      NodeName to_node,
      PortName port) = 0;

  virtual int Send_PeerClosed(
      NodeName to_node,
      PortName port) = 0;

  // Expected to call Node's GetMessage method to access the next available
  // message. There may be zero or more messages available.
  virtual int MessagesAvailable(
      PortName port) = 0;
};

class Node {
 public:
  Node(NodeName name, NodeDelegate* delegate);
  ~Node();

  int GetMessage(
      PortName port,
      Message** message);

  int SendMessage(
      PortName port,
      Message* message); 

  int AcceptMessage(
      PortName port,
      Message* message);

  int AcceptPort(
      PortName port,
      PortName peer,
      NodeName peer_node,
      uint32_t next_sequence_num);

  int AcceptPortAck(
      PortName port);

  int UpdatePort(
      PortName port,
      NodeName peer_node);

  int UpdatePortAck(
      PortName port);

  int PeerClosed(
      PortName port);

 private:
  struct Impl;
  Impl* impl_;
};

}  // namespace ports

#endif  // PORTS_INCLUDE_PORTS_H_
