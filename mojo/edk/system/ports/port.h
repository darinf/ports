// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_EDK_SYSTEM_PORTS_PORT_H_
#define MOJO_EDK_SYSTEM_PORTS_PORT_H_

#include <memory>
#include <mutex>
#include <queue>
#include <utility>
#include <vector>

#include "mojo/edk/system/ports/message_queue.h"
#include "mojo/edk/system/ports/user_data.h"

namespace mojo {
namespace edk {
namespace ports {

class Port {
 public:
  enum State {
    kUninitialized,
    kReceiving,
    kBuffering,
    kProxying,
    kClosed
  };

  std::mutex lock;
  State state;
  NodeName peer_node_name;
  PortName peer_port_name;
  uint64_t next_sequence_num_to_send;
  uint64_t last_sequence_num_to_receive;
  MessageQueue message_queue;
  std::unique_ptr<std::pair<NodeName, ScopedMessage>> send_on_proxy_removal;
  std::shared_ptr<UserData> user_data;
  bool remove_proxy_on_last_message;
  bool peer_closed;

  std::queue<ScopedMessage> outgoing_messages;
  std::vector<std::shared_ptr<Port>> outgoing_ports;

  Port(uint64_t next_sequence_num_to_send,
       uint64_t next_sequence_num_to_receive);
  ~Port();
};

}  // namespace ports
}  // namespace edk
}  // namespace mojo

#endif  // MOJO_EDK_SYSTEM_PORTS_PORT_H_
