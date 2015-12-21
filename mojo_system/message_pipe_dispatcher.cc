// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ports/mojo_system/message_pipe_dispatcher.h"


namespace mojo {
namespace edk {

MessagePipeDispatcher::MessagePipeDispatcher(Node* node,
                                             const ports::PortName& port_name)
    : node_(node), port_name_(port_name) {}

Dispatcher::Type MessagePipeDispatcher::GetType() const {
  return Type::MESSAGE_PIPE;
}

MessagePipeDispatcher::~MessagePipeDispatcher() {}

}  // namespace edk
}  // namespace mojo
