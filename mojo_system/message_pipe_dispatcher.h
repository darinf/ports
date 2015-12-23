// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PORTS_MOJO_SYSTEM_MESSAGE_PIPE_DISPATCHER_H_
#define PORTS_MOJO_SYSTEM_MESSAGE_PIPE_DISPATCHER_H_

#include <queue>

#include "base/macros.h"
#include "mojo/edk/system/awakable_list.h"
#include "ports/include/ports.h"
#include "ports/mojo_system/dispatcher.h"
#include "ports/mojo_system/node.h"

namespace mojo {
namespace edk {

class MessagePipeDispatcher : public Dispatcher, public Node::PortObserver {
 public:
  // Create a MessagePipeDispatcher for port |port_name| on |node|.
  MessagePipeDispatcher(Node* node, const ports::PortName& port_name);

  Type GetType() const override;

  const ports::PortName& GetPortName() const { return port_name_; }

 private:
  ~MessagePipeDispatcher() override;

  // Dispatcher:
  void CloseImplNoLock() override;
  MojoResult WriteMessageImplNoLock(const void* bytes,
                                    uint32_t num_bytes,
                                    const DispatcherInTransit* dispatchers,
                                    uint32_t num_dispatchers,
                                    MojoWriteMessageFlags flags) override;
  MojoResult ReadMessageImplNoLock(void* bytes,
                                   uint32_t* num_bytes,
                                   MojoHandle* handles,
                                   uint32_t* num_handles,
                                   MojoReadMessageFlags flags) override;
  HandleSignalsState GetHandleSignalsStateImplNoLock() const override;
  MojoResult AddAwakableImplNoLock(Awakable* awakable,
                                   MojoHandleSignals signals,
                                   uintptr_t context,
                                   HandleSignalsState* signals_state) override;
  void RemoveAwakableImplNoLock(Awakable* awakable,
                                HandleSignalsState* signals_state) override;

  bool BeginTransitImplNoLock() override;
  void EndTransitImplNoLock(bool canceled) override;

  // Node::PortObserver:
  void OnMessageAvailable(const ports::PortName& port,
                          ports::ScopedMessage message) override;
  void OnPeerClosed(const ports::PortName& port) override;

  Node* node_;
  const ports::PortName port_name_;

  bool peer_closed_ = false;
  bool port_transferred_ = false;
  AwakableList awakables_;
  std::queue<ports::ScopedMessage> incoming_messages_;

  DISALLOW_COPY_AND_ASSIGN(MessagePipeDispatcher);
};

}  // namespace edk
}  // namespace mojo

#endif  // PORTS_MOJO_SYSTEM_MESSAGE_PIPE_DISPATCHER_H_
