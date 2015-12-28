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

#ifndef PORTS_SRC_EVENT_H_
#define PORTS_SRC_EVENT_H_

#include <stdint.h>
#include <string.h>

#include "ports/include/ports.h"

namespace ports {

struct PortDescriptor {
  NodeName peer_node_name;
  PortName peer_port_name;
  NodeName referring_node_name;
  PortName referring_port_name;
  uint32_t next_sequence_num_to_send;
  uint32_t next_sequence_num_to_receive;
};

enum struct EventType : uint32_t {
  kUser,
  kPortAccepted,
  kObserveProxy,
  kObserveProxyAck,
  kObserveClosure,
};

struct EventHeader {
  EventType type;
  uint32_t padding;
  PortName port_name;
};

struct UserEventData {
  uint32_t sequence_num;
  uint32_t num_ports;
};

struct ObserveProxyEventData {
  NodeName proxy_node_name;
  PortName proxy_port_name;
  NodeName proxy_to_node_name;
  PortName proxy_to_port_name;
};

struct ObserveProxyAckEventData {
  uint32_t last_sequence_num;
  uint32_t padding;
};

struct ObserveClosureEventData {
  uint32_t last_sequence_num;
  uint32_t padding;
};

inline const EventHeader* GetEventHeader(const ScopedMessage& message) {
  return static_cast<const EventHeader*>(message->header_bytes());
}

inline EventHeader* GetMutableEventHeader(ScopedMessage& message) {
  return static_cast<EventHeader*>(message->mutable_header_bytes());
}

template <typename EventData>
inline const EventData* GetEventData(const ScopedMessage& message) {
  return reinterpret_cast<const EventData*>(
      reinterpret_cast<const char*>(GetEventHeader(message) + 1));
}

template <typename EventData>
inline EventData* GetMutableEventData(ScopedMessage& message) {
  return reinterpret_cast<EventData*>(
      reinterpret_cast<char*>(GetMutableEventHeader(message) + 1));
}

inline const PortDescriptor* GetPortDescriptors(const UserEventData* event) {
  return reinterpret_cast<const PortDescriptor*>(
      reinterpret_cast<const char*>(event + 1));
}

inline PortDescriptor* GetMutablePortDescriptors(UserEventData* event) {
  return reinterpret_cast<PortDescriptor*>(reinterpret_cast<char*>(event + 1));
}

}  // namespace ports

#endif  // PORTS_SRC_EVENT_H_
