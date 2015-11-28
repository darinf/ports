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

#include <memory>
#include <mutex>
#include <unordered_map>

#include "../include/ports.h"
#include "port.h"
#include "port_name_hash.h"

namespace ports {

class Node::Impl {
 public:
  Impl(NodeName name, NodeDelegate* delegate);
  ~Impl();

  int CreatePortPair(PortName* port0, PortName* port1);
  int GetMessage(PortName port_name, Message** message);
  int SendMessage(PortName port_name, Message* message); 
  int AcceptMessage(PortName port_name, Message* message);
  int AcceptPort(PortName port_name,
                 PortName peer_name,
                 NodeName peer_node_name,
                 uint32_t next_sequence_num,
                 NodeName from_node_name,
                 PortName from_port_name,
                 PortName dependent_port_name);
  int AcceptPortAck(PortName port_name);
  int UpdatePort(PortName port_name,
                 PortName peer_name,
                 NodeName peer_node_name);
  int UpdatePortAck(PortName port_name);
  int PeerClosed(PortName port_name);

 private:
  NodeName name_;
  NodeDelegate* delegate_;

  std::shared_ptr<Port> GetPort(PortName port_name);
  int RenameAndSendPort(NodeName node_name,
                        PortName dependent_peer_name,
                        PortName* port_name);

  std::mutex ports_lock_;
  std::unordered_map<PortName, std::shared_ptr<Port>> ports_;
};

}  // namespace ports

#endif  // PORTS_SRC_NODE_IMPL_H_
