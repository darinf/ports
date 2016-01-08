// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_EDK_SYSTEM_MESSAGE_PIPE_DISPATCHER_H_
#define MOJO_EDK_SYSTEM_MESSAGE_PIPE_DISPATCHER_H_

#include <queue>

#include "base/macros.h"
#include "mojo/edk/system/awakable_list.h"
#include "mojo/edk/system/dispatcher.h"
#include "mojo/edk/system/ports/ports.h"

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
  MojoResult Close() override;
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
  void StartSerialize(uint32_t* num_bytes,
                      uint32_t* num_ports,
                      uint32_t* num_handles) override;
  bool EndSerializeAndClose(void* destination,
                            ports::PortName* ports,
                            PlatformHandleVector* handles) override;
  void CompleteTransit() override;

  static scoped_refptr<Dispatcher> Deserialize(
      const void* data,
      size_t num_bytes,
      const ports::PortName* ports,
      size_t num_ports,
      PlatformHandle* handles,
      size_t num_handles);

 private:
  class PortObserverThunk;
  friend class PortObserverThunk;

  ~MessagePipeDispatcher() override;

  HandleSignalsState GetHandleSignalsStateNoLock() const;
  void OnPortStatusChanged();

  // These are safe to access from any thread without locking.
  Node* const node_;
  const ports::PortRef port_;

  // Guards access to all the fields below.
  mutable base::Lock signal_lock_;

  bool port_transferred_ = false;
  bool port_closed_ = false;
  AwakableList awakables_;

  DISALLOW_COPY_AND_ASSIGN(MessagePipeDispatcher);
};

}  // namespace edk
}  // namespace mojo

#endif  // MOJO_EDK_SYSTEM_MESSAGE_PIPE_DISPATCHER_H_
