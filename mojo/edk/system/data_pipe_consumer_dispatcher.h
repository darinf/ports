// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_EDK_SYSTEM_DATA_PIPE_CONSUMER_DISPATCHER_H_
#define MOJO_EDK_SYSTEM_DATA_PIPE_CONSUMER_DISPATCHER_H_

#include <stddef.h>
#include <stdint.h>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/synchronization/lock.h"
#include "mojo/edk/system/awakable_list.h"
#include "mojo/edk/system/dispatcher.h"
#include "mojo/edk/system/ports/port_ref.h"
#include "mojo/edk/system/system_impl_export.h"

namespace mojo {
namespace edk {

class NodeController;

// This is the Dispatcher implementation for the consumer handle for data
// pipes created by the Mojo primitive MojoCreateDataPipe(). This class is
// thread-safe.
class MOJO_SYSTEM_IMPL_EXPORT DataPipeConsumerDispatcher final
    : public Dispatcher {
 public:
  DataPipeConsumerDispatcher(NodeController* node_controller,
                             const ports::PortRef& port,
                             const MojoCreateDataPipeOptions& options);

  // Dispatcher:
  Type GetType() const override;
  MojoResult Close() override;
  MojoResult ReadData(void* elements,
                      uint32_t* num_bytes,
                      MojoReadDataFlags flags) override;
  MojoResult BeginReadData(const void** buffer,
                           uint32_t* buffer_num_bytes,
                           MojoReadDataFlags flags) override;
  MojoResult EndReadData(uint32_t num_bytes_read) override;
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
  bool BeginTransit() override;
  void CompleteTransit() override;
  void CancelTransit() override;

  static scoped_refptr<DataPipeConsumerDispatcher>
  Deserialize(const void* data,
              size_t num_bytes,
              const ports::PortName* ports,
              size_t num_ports,
              PlatformHandle* handles,
              size_t num_handles);

 private:
  class PortObserverThunk;
  friend class PortObserverThunk;

  ~DataPipeConsumerDispatcher() override;

  MojoResult CloseNoLock();
  HandleSignalsState GetHandleSignalsStateNoLock() const;
  void OnPortStatusChanged();
  void UpdateSignalsStateNoLock();

  const MojoCreateDataPipeOptions options_;
  NodeController* const node_controller_;
  const ports::PortRef port_;

  mutable base::Lock lock_;

  // Queue of incoming messages.
  std::vector<char> data_;
  AwakableList awakable_list_;

  bool in_two_phase_read_ = false;
  uint32_t two_phase_max_bytes_read_ = 0;

  // If we get data from the port while we're in two-phase read, we can't
  // resize data_ since it's being used. So instead we store it temporarly.
  std::vector<char> data_received_during_two_phase_read_;

  bool error_ = false;
  bool port_closed_ = false;
  bool port_transferred_ = false;

  DISALLOW_COPY_AND_ASSIGN(DataPipeConsumerDispatcher);
};

}  // namespace edk
}  // namespace mojo

#endif  // MOJO_EDK_SYSTEM_DATA_PIPE_CONSUMER_DISPATCHER_H_
