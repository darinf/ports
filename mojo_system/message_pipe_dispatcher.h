// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PORTS_MOJO_SYSTEM_MESSAGE_PIPE_DISPATCHER_H_
#define PORTS_MOJO_SYSTEM_MESSAGE_PIPE_DISPATCHER_H_

#include "base/macros.h"
#include "ports/include/ports.h"
#include "ports/mojo_system/dispatcher.h"
#include "ports/mojo_system/node.h"

namespace mojo {
namespace edk {

class MessagePipeDispatcher : public Dispatcher {
 public:
  MessagePipeDispatcher(Node* node, const ports::PortName& port_name);

  Type GetType() const override;

 private:
  ~MessagePipeDispatcher() override;

  Node* node_;
  ports::PortName port_name_;

  DISALLOW_COPY_AND_ASSIGN(MessagePipeDispatcher);
};

}  // namespace edk
}  // namespace mojo

#endif  // PORTS_MOJO_SYSTEM_MESSAGE_PIPE_DISPATCHER_H_
