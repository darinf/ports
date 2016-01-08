// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_EDK_SYSTEM_PORTS_EVENT_H_
#define MOJO_EDK_SYSTEM_PORTS_EVENT_H_

#include <stdint.h>

#include "mojo/edk/system/ports/message.h"
#include "mojo/edk/system/ports/name.h"

namespace mojo {
namespace edk {
namespace ports {

// TODO: Add static assertions of alignment.

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
}  // namespace edk
}  // namespace mojo

#endif  // MOJO_EDK_SYSTEM_PORTS_EVENT_H_
