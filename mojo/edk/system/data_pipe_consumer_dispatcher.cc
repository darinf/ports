// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/edk/system/data_pipe_consumer_dispatcher.h"

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <utility>

#include "base/bind.h"
#include "base/logging.h"
#include "base/message_loop/message_loop.h"
#include "mojo/edk/embedder/embedder_internal.h"
#include "mojo/edk/embedder/platform_shared_buffer.h"
#include "mojo/edk/embedder/platform_support.h"
#include "mojo/edk/system/core.h"
#include "mojo/edk/system/data_pipe.h"
#include "mojo/edk/system/node_controller.h"

namespace mojo {
namespace edk {

namespace {

struct SerializedState {
  MojoCreateDataPipeOptions options;
  bool error;
};

}  // namespace

// A PortObserver which forwards to a DataPipeConsumerDispatcher. This owns a
// reference to the dispatcher to ensure it lives as long as the observed port.
class DataPipeConsumerDispatcher::PortObserverThunk
    : public NodeController::PortObserver {
 public:
  explicit PortObserverThunk(
      scoped_refptr<DataPipeConsumerDispatcher> dispatcher)
      : dispatcher_(dispatcher) {}
  ~PortObserverThunk() override {}

 private:
  // NodeController::PortObserver:
  void OnPortStatusChanged() override { dispatcher_->OnPortStatusChanged(); }

  scoped_refptr<DataPipeConsumerDispatcher> dispatcher_;

  DISALLOW_COPY_AND_ASSIGN(PortObserverThunk);
};

DataPipeConsumerDispatcher::DataPipeConsumerDispatcher(
    NodeController* node_controller,
    const ports::PortRef& port,
    const MojoCreateDataPipeOptions& options)
    : options_(options),
      node_controller_(node_controller),
      port_(port) {
  // OnPortStatusChanged (via PortObserverThunk) may be called before this
  // constructor returns. Hold a lock here to prevent signal races.
  base::AutoLock lock(lock_);
  node_controller_->SetPortObserver(port_,
                                    std::make_shared<PortObserverThunk>(this));
}

Dispatcher::Type DataPipeConsumerDispatcher::GetType() const {
  return Type::DATA_PIPE_CONSUMER;
}

MojoResult DataPipeConsumerDispatcher::Close() {
  base::AutoLock lock(lock_);
  return CloseNoLock();
}

MojoResult DataPipeConsumerDispatcher::ReadData(void* elements,
                                                uint32_t* num_bytes,
                                                MojoReadDataFlags flags) {
  base::AutoLock lock(lock_);
  if (port_closed_)
    return MOJO_RESULT_INVALID_ARGUMENT;

  if (in_two_phase_read_)
    return MOJO_RESULT_BUSY;

  if ((flags & MOJO_READ_DATA_FLAG_QUERY)) {
    if ((flags & MOJO_READ_DATA_FLAG_PEEK) ||
        (flags & MOJO_READ_DATA_FLAG_DISCARD))
      return MOJO_RESULT_INVALID_ARGUMENT;
    DCHECK(!(flags & MOJO_READ_DATA_FLAG_DISCARD));  // Handled above.
    DVLOG_IF(2, elements)
        << "Query mode: ignoring non-null |elements|";
    *num_bytes = static_cast<uint32_t>(data_.size());
    return MOJO_RESULT_OK;
  }

  bool discard = false;
  if ((flags & MOJO_READ_DATA_FLAG_DISCARD)) {
    // These flags are mutally exclusive.
    if (flags & MOJO_READ_DATA_FLAG_PEEK)
      return MOJO_RESULT_INVALID_ARGUMENT;
    DVLOG_IF(2, elements)
        << "Discard mode: ignoring non-null |elements|";
    discard = true;
  }

  uint32_t max_num_bytes_to_read = *num_bytes;
  if (max_num_bytes_to_read % options_.element_num_bytes != 0)
    return MOJO_RESULT_INVALID_ARGUMENT;

  bool all_or_none = flags & MOJO_READ_DATA_FLAG_ALL_OR_NONE;
  uint32_t min_num_bytes_to_read =
      all_or_none ? max_num_bytes_to_read : 0;

  if (min_num_bytes_to_read > data_.size())
    return error_ ? MOJO_RESULT_FAILED_PRECONDITION : MOJO_RESULT_OUT_OF_RANGE;

  uint32_t bytes_to_read = std::min(max_num_bytes_to_read,
                                    static_cast<uint32_t>(data_.size()));
  if (bytes_to_read == 0)
    return error_ ? MOJO_RESULT_FAILED_PRECONDITION : MOJO_RESULT_SHOULD_WAIT;

  if (!discard)
    memcpy(elements, &data_[0], bytes_to_read);
  *num_bytes = bytes_to_read;

  bool peek = !!(flags & MOJO_READ_DATA_FLAG_PEEK);
  if (discard || !peek)
    data_.erase(data_.begin(), data_.begin() + bytes_to_read);

  return MOJO_RESULT_OK;
}

MojoResult DataPipeConsumerDispatcher::BeginReadData(const void** buffer,
                                                     uint32_t* buffer_num_bytes,
                                                     MojoReadDataFlags flags) {
  base::AutoLock lock(lock_);
  if (port_closed_)
    return MOJO_RESULT_INVALID_ARGUMENT;

  if (in_two_phase_read_)
    return MOJO_RESULT_BUSY;

  // These flags may not be used in two-phase mode.
  if ((flags & MOJO_READ_DATA_FLAG_DISCARD) ||
      (flags & MOJO_READ_DATA_FLAG_QUERY) ||
      (flags & MOJO_READ_DATA_FLAG_PEEK))
    return MOJO_RESULT_INVALID_ARGUMENT;

  uint32_t max_num_bytes_to_read = static_cast<uint32_t>(data_.size());
  if (max_num_bytes_to_read == 0)
    return error_ ? MOJO_RESULT_FAILED_PRECONDITION : MOJO_RESULT_SHOULD_WAIT;

  in_two_phase_read_ = true;
  *buffer = &data_[0];
  *buffer_num_bytes = max_num_bytes_to_read;
  two_phase_max_bytes_read_ = max_num_bytes_to_read;

  return MOJO_RESULT_OK;
}

MojoResult DataPipeConsumerDispatcher::EndReadData(uint32_t num_bytes_read) {
  base::AutoLock lock(lock_);
  if (port_closed_)
    return MOJO_RESULT_INVALID_ARGUMENT;

  if (!in_two_phase_read_)
    return MOJO_RESULT_FAILED_PRECONDITION;

  HandleSignalsState old_state = GetHandleSignalsStateNoLock();
  MojoResult rv;
  if (num_bytes_read > two_phase_max_bytes_read_ ||
      num_bytes_read % options_.element_num_bytes != 0) {
    rv = MOJO_RESULT_INVALID_ARGUMENT;
  } else {
    rv = MOJO_RESULT_OK;
    data_.erase(data_.begin(), data_.begin() + num_bytes_read);
  }

  in_two_phase_read_ = false;
  two_phase_max_bytes_read_ = 0;
  if (!data_received_during_two_phase_read_.empty()) {
    if (data_.empty()) {
      data_received_during_two_phase_read_.swap(data_);
    } else {
      data_.insert(data_.end(), data_received_during_two_phase_read_.begin(),
                   data_received_during_two_phase_read_.end());
      data_received_during_two_phase_read_.clear();
    }
  }

  HandleSignalsState new_state = GetHandleSignalsStateNoLock();
  if (!new_state.equals(old_state))
    awakable_list_.AwakeForStateChange(new_state);

  return rv;
}

HandleSignalsState DataPipeConsumerDispatcher::GetHandleSignalsState() const {
  base::AutoLock lock(lock_);
  return GetHandleSignalsStateNoLock();
}

MojoResult DataPipeConsumerDispatcher::AddAwakable(
    Awakable* awakable,
    MojoHandleSignals signals,
    uintptr_t context,
    HandleSignalsState* signals_state) {
  base::AutoLock lock(lock_);
  HandleSignalsState state = GetHandleSignalsStateNoLock();
  if (state.satisfies(signals)) {
    if (signals_state)
      *signals_state = state;
    return MOJO_RESULT_ALREADY_EXISTS;
  }
  if (!state.can_satisfy(signals)) {
    if (signals_state)
      *signals_state = state;
    return MOJO_RESULT_FAILED_PRECONDITION;
  }

  awakable_list_.Add(awakable, signals, context);
  return MOJO_RESULT_OK;
}

void DataPipeConsumerDispatcher::RemoveAwakable(
    Awakable* awakable,
    HandleSignalsState* signals_state) {
  base::AutoLock lock(lock_);
  awakable_list_.Remove(awakable);
  if (signals_state)
    *signals_state = GetHandleSignalsStateNoLock();
}

void DataPipeConsumerDispatcher::StartSerialize(uint32_t* num_bytes,
                                                uint32_t* num_ports,
                                                uint32_t* num_handles) {
  *num_bytes = sizeof(SerializedState) + data_.size();
  *num_ports = 1;
  *num_handles = 0;
}

bool DataPipeConsumerDispatcher::EndSerializeAndClose(
    void* destination,
    ports::PortName* ports,
    PlatformHandleVector* platform_handles) {
  SerializedState* state = static_cast<SerializedState*>(destination);
  memcpy(&state->options, &options_, sizeof(MojoCreateDataPipeOptions));
  state->error = error_;

  memcpy(state + 1, data_.data(), data_.size());

  ports[0] = port_.name();

  port_transferred_ = true;
  CloseNoLock();

  return true;
}

bool DataPipeConsumerDispatcher::BeginTransit() {
  lock_.Acquire();
  return !in_two_phase_read_;
}

void DataPipeConsumerDispatcher::CompleteTransit() {
  lock_.Release();
}

void DataPipeConsumerDispatcher::CancelTransit() { lock_.Release(); }

// static
scoped_refptr<DataPipeConsumerDispatcher>
DataPipeConsumerDispatcher::Deserialize(const void* data,
                                        size_t num_bytes,
                                        const ports::PortName* ports,
                                        size_t num_ports,
                                        PlatformHandle* handles,
                                        size_t num_handles) {
  if (num_ports != 1 || num_handles != 0)
    return nullptr;

  if (num_bytes < sizeof(SerializedState))
    return nullptr;

  const SerializedState* state = static_cast<const SerializedState*>(data);
  size_t data_buffer_size = num_bytes - sizeof(MojoCreateDataPipeOptions);

  NodeController* node_controller = internal::g_core->node_controller();
  ports::PortRef port;
  if (node_controller->node()->GetPort(ports[0], &port) != ports::OK)
    return nullptr;

  scoped_refptr<DataPipeConsumerDispatcher> dispatcher =
      new DataPipeConsumerDispatcher(node_controller, port, state->options);

  dispatcher->error_ = state->error;
  dispatcher->data_.resize(data_buffer_size);
  memcpy(dispatcher->data_.data(), state + 1, data_buffer_size);

  dispatcher->OnPortStatusChanged();

  return dispatcher;
}

DataPipeConsumerDispatcher::~DataPipeConsumerDispatcher() {}

MojoResult DataPipeConsumerDispatcher::CloseNoLock() {
  lock_.AssertAcquired();
  if (port_closed_)
    return MOJO_RESULT_INVALID_ARGUMENT;
  port_closed_ = true;

  if (!port_transferred_)
    node_controller_->node()->ClosePort(port_);
  awakable_list_.CancelAll();

  return MOJO_RESULT_OK;
}

HandleSignalsState
DataPipeConsumerDispatcher::GetHandleSignalsStateNoLock() const {
  lock_.AssertAcquired();

  HandleSignalsState rv;
  if (!data_.empty()) {
    if (!in_two_phase_read_)
      rv.satisfied_signals |= MOJO_HANDLE_SIGNAL_READABLE;
    rv.satisfiable_signals |= MOJO_HANDLE_SIGNAL_READABLE;
  } else if (!error_) {
    rv.satisfiable_signals |= MOJO_HANDLE_SIGNAL_READABLE;
  }

  if (error_)
    rv.satisfied_signals |= MOJO_HANDLE_SIGNAL_PEER_CLOSED;
  rv.satisfiable_signals |= MOJO_HANDLE_SIGNAL_PEER_CLOSED;
  return rv;
}

void DataPipeConsumerDispatcher::UpdateSignalsStateNoLock() {
  lock_.AssertAcquired();

  bool had_error = error_;
  size_t data_size = data_.size();

  ports::PortStatus port_status;
  if (node_controller_->node()->GetStatus(port_, &port_status) != ports::OK ||
      port_status.peer_closed) {
    error_ = true;
  }

  if (port_status.has_messages) {
    ports::ScopedMessage message;
    do {
      int rv = node_controller_->node()->GetMessageIf(port_, nullptr, &message);
      if (rv != ports::OK)
        error_ = true;
      if (message) {
        if (in_two_phase_read_) {
          size_t data_size = data_received_during_two_phase_read_.size();
          DCHECK_GE(options_.capacity_num_bytes, data_.size() + data_size);
          size_t bytes_to_read =
              std::min(options_.capacity_num_bytes - data_size - data_.size(),
                       message->num_payload_bytes());
          if (bytes_to_read > 0) {
            data_received_during_two_phase_read_.resize(
                data_size + bytes_to_read);
            memcpy(data_received_during_two_phase_read_.data() + data_size,
                   message->payload_bytes(), bytes_to_read);
          }
        } else {
          size_t data_size = data_.size();
          DCHECK_GE(options_.capacity_num_bytes, data_size);
          size_t bytes_to_read =
              std::min(options_.capacity_num_bytes - data_size,
                       message->num_payload_bytes());
          if (bytes_to_read > 0) {
            data_.resize(data_size + bytes_to_read);
            memcpy(data_.data() + data_size, message->payload_bytes(),
                   bytes_to_read);
          }
        }
      }
    } while (message);
  }

  if (error_ != had_error || data_size != data_.size())
    awakable_list_.AwakeForStateChange(GetHandleSignalsStateNoLock());
}

void DataPipeConsumerDispatcher::OnPortStatusChanged() {
  base::AutoLock lock(lock_);
  UpdateSignalsStateNoLock();
}

}  // namespace edk
}  // namespace mojo
