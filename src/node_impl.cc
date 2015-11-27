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

#include "node_impl.h"

namespace ports {

Node::Impl::Impl(NodeDelegate* delegate)
    : delegate_(delegate) {
}

Node::Impl::~Impl() {
}

int Node::Impl::GetMessage(
    PortName port,
    Message** message) {
  return ERROR;
}

int Node::Impl::SendMessage(
    PortName port,
    Message* message) {
  return ERROR;
}

int Node::Impl::AcceptMessage(
    PortName port,
    Message* message) {
  return ERROR;
}

int Node::Impl::AcceptPort(
    PortName port,
    PortName peer,
    NodeName peer_node,
    uint32_t next_sequence_num) {
  return ERROR;
}

int Node::Impl::AcceptPortAck(
    PortName port) {
  return ERROR;
}

int Node::Impl::UpdatePort(
    PortName port,
    NodeName peer_node) {
  return ERROR;
}

int Node::Impl::UpdatePortAck(
    PortName port) {
  return ERROR;
}

int Node::Impl::PeerClosed(
    PortName port) {
  return ERROR;
}

std::shared_ptr<Port> Node::Impl::GetPort(PortName port) {
  std::lock_guard<std::mutex> locker(ports_lock_);

  auto iter = ports_.find(port);
  if (iter == ports_.end())
    return std::shared_ptr<Port>();

  return iter->second;
}

}  // namespace ports
