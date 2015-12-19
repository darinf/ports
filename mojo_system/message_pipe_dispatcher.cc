// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ports/mojo_system/message_pipe_dispatcher.h"

namespace mojo {
namespace edk {

MessagePipeDispatcher::MessagePipeDispatcher() {}

Dispatcher::Type MessagePipeDispatcher::GetType() const {
  return Type::MESSAGE_PIPE;
}

MojoResult MessagePipeDispatcher::WriteMessageImplNoLock(
    ports::ScopedMessage message,
    MojoWriteMessageFlags flags) {
  return MOJO_RESULT_OK;
}

MojoResult MessagePipeDispatcher::ReadMessageImplNoLock(
    MojoReadMessageFlags flags,
    ports::ScopedMessage* message) {
  return MOJO_RESULT_OK;
}

HandleSignalsState
MessagePipeDispatcher::GetHandleSignalsStateImplNoLock() const {
  return HandleSignalsState();
}

MessagePipeDispatcher::~MessagePipeDispatcher() {}

}  // namespace edk
}  // namespace mojo
