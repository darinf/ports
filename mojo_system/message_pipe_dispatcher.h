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

namespace mojo {
namespace edk {

class Node;

class MessagePipeDispatcher : public Dispatcher {
 public:
  // Create a MessagePipeDispatcher for port |port_name| on |node|.
  MessagePipeDispatcher(Node* node, const ports::PortRef& port);

  const ports::PortName& GetPortName() const { return port_.name(); }

  // Dispatcher:
  Type GetType() const override;
  void Close() override;
  MojoResult WriteMessage(const void* bytes,
                          uint32_t num_bytes,
                          const DispatcherInTransit* dispatchers,
                          uint32_t num_dispatchers,
                          MojoWriteMessageFlags flags) override;
  MojoResult ReadMessage(void* bytes,
                         uint32_t* num_bytes,
                         MojoHandle* handles,
                         uint32_t* num_handles,
                         MojoReadMessageFlags flags) override;
  HandleSignalsState GetHandleSignalsState() const override;
  MojoResult AddAwakable(Awakable* awakable,
                         MojoHandleSignals signals,
                         uintptr_t context,
                         HandleSignalsState* signals_state) override;
  void RemoveAwakable(Awakable* awakable,
                      HandleSignalsState* signals_state) override;
  void GetSerializedSize(uint32_t* num_bytes, uint32_t* num_handles) override;
  bool SerializeAndClose(void* destination,
                         PlatformHandleVector* handles) override;
  void CompleteTransit() override;

 private:
  class PortObserverThunk;
  friend class PortObserverThunk;

  ~MessagePipeDispatcher() override;

  HandleSignalsState GetHandleSignalsStateNoLock() const;
  bool UpdateSignalsStateNoLock();

  // Called by PortObserverThunk when messages are available on the port.
  void OnMessagesAvailable();

  // These are safe to access from any thread without locking.
  Node* const node_;
  const ports::PortRef port_;

  // Guards access to all the fields below.
  mutable base::Lock signal_lock_;

  bool peer_closed_ = false;
  bool port_transferred_ = false;
  bool port_readable_ = false;
  bool port_closed_ = false;
  AwakableList awakables_;

  DISALLOW_COPY_AND_ASSIGN(MessagePipeDispatcher);
};

}  // namespace edk
}  // namespace mojo

#endif  // PORTS_MOJO_SYSTEM_MESSAGE_PIPE_DISPATCHER_H_
