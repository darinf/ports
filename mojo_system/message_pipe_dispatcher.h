// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PORTS_MOJO_SYSTEM_MESSAGE_PIPE_DISPATCHER_H_
#define PORTS_MOJO_SYSTEM_MESSAGE_PIPE_DISPATCHER_H_

#include "base/macros.h"
#include "mojo/edk/embedder/scoped_platform_handle.h"
#include "ports/mojo_system/dispatcher.h"

namespace mojo {
namespace edk {

class MessagePipeDispatcher : public Dispatcher {
 public:
  MessagePipeDispatcher();

  Type GetType() const override;

 private:
  // Dispatcher:
  MojoResult WriteMessageImplNoLock(ports::ScopedMessage message,
                                    MojoWriteMessageFlags flags) override;
  MojoResult ReadMessageImplNoLock(MojoReadMessageFlags flags,
                                   ports::ScopedMessage* message) override;
  HandleSignalsState GetHandleSignalsStateImplNoLock() const override;

  ~MessagePipeDispatcher() override;

  DISALLOW_COPY_AND_ASSIGN(MessagePipeDispatcher);
};

}  // namespace edk
}  // namespace mojo

#endif  // PORTS_MOJO_SYSTEM_MESSAGE_PIPE_DISPATCHER_H_
