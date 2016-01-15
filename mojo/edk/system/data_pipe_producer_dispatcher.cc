// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/edk/system/data_pipe_producer_dispatcher.h"

#include <stddef.h>
#include <stdint.h>

#include <utility>

#include "base/bind.h"
#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/message_loop/message_loop.h"
#include "mojo/edk/embedder/embedder_internal.h"
#include "mojo/edk/embedder/platform_shared_buffer.h"
#include "mojo/edk/embedder/platform_support.h"
#include "mojo/edk/system/configuration.h"
#include "mojo/edk/system/core.h"
#include "mojo/edk/system/data_pipe.h"
#include "mojo/edk/system/node_controller.h"
#include "mojo/edk/system/ports_message.h"

namespace mojo {
namespace edk {

namespace {

struct SerializedState {
  MojoCreateDataPipeOptions options;
  bool error;
};

}  // namespace

// A PortObserver which forwards to a DataPipeProducerDispatcher. This owns a
// reference to the dispatcher to ensure it lives as long as the observed port.
class DataPipeProducerDispatcher::PortObserverThunk
    : public NodeController::PortObserver {
 public:
  explicit PortObserverThunk(
      scoped_refptr<DataPipeProducerDispatcher> dispatcher)
      : dispatcher_(dispatcher) {}

 private:
  ~PortObserverThunk() override {}

  // NodeController::PortObserver:
  void OnPortStatusChanged() override { dispatcher_->OnPortStatusChanged(); }

  scoped_refptr<DataPipeProducerDispatcher> dispatcher_;

  DISALLOW_COPY_AND_ASSIGN(PortObserverThunk);
};

DataPipeProducerDispatcher::DataPipeProducerDispatcher(
    NodeController* node_controller,
    const ports::PortRef& port,
    const MojoCreateDataPipeOptions& options)
    : options_(options),
      node_controller_(node_controller),
      port_(port) {
  // OnPortStatusChanged (via PortObserverThunk) may be called before this
  // constructor returns. Hold a lock here to prevent signal races.
  base::AutoLock lock(lock_);
  node_controller_->SetPortObserver(
      port_,
      make_scoped_refptr(new PortObserverThunk(this)));
}

Dispatcher::Type DataPipeProducerDispatcher::GetType() const {
  return Type::DATA_PIPE_PRODUCER;
}

MojoResult DataPipeProducerDispatcher::Close() {
  base::AutoLock lock(lock_);
  return CloseNoLock();
}

MojoResult DataPipeProducerDispatcher::WriteData(const void* elements,
                                                 uint32_t* num_bytes,
                                                 MojoWriteDataFlags flags) {
  base::AutoLock lock(lock_);
  if (port_closed_ || in_transit_)
    return MOJO_RESULT_INVALID_ARGUMENT;

  if (InTwoPhaseWriteNoLock())
    return MOJO_RESULT_BUSY;
  if (error_)
    return MOJO_RESULT_FAILED_PRECONDITION;
  if (*num_bytes % options_.element_num_bytes != 0)
    return MOJO_RESULT_INVALID_ARGUMENT;
  if (*num_bytes == 0)
    return MOJO_RESULT_OK;  // Nothing to do.

  // For now, we ignore options.capacity_num_bytes as a total of all pending
  // writes (and just treat it per message). We will implement that later if
  // we need to. All current uses want all their data to be sent, and it's not
  // clear that this backpressure should be done at the mojo layer or at a
  // higher application layer.
  bool all_or_none = flags & MOJO_WRITE_DATA_FLAG_ALL_OR_NONE;
  uint32_t min_num_bytes_to_write = all_or_none ? *num_bytes : 0;
  if (min_num_bytes_to_write > options_.capacity_num_bytes) {
    // Don't return "should wait" since you can't wait for a specified amount of
    // data.
    return MOJO_RESULT_OUT_OF_RANGE;
  }

  uint32_t num_bytes_to_write =
      std::min(*num_bytes, options_.capacity_num_bytes);
  if (num_bytes_to_write == 0)
    return MOJO_RESULT_SHOULD_WAIT;

  HandleSignalsState old_state = GetHandleSignalsStateNoLock();

  *num_bytes = num_bytes_to_write;
  WriteDataIntoMessagesNoLock(elements, num_bytes_to_write);

  HandleSignalsState new_state = GetHandleSignalsStateNoLock();
  if (!new_state.equals(old_state))
    awakable_list_.AwakeForStateChange(new_state);
  return MOJO_RESULT_OK;
}

MojoResult DataPipeProducerDispatcher::BeginWriteData(
    void** buffer,
    uint32_t* buffer_num_bytes,
    MojoWriteDataFlags flags) {
  base::AutoLock lock(lock_);
  if (port_closed_ || in_transit_)
    return MOJO_RESULT_INVALID_ARGUMENT;

  if (InTwoPhaseWriteNoLock())
    return MOJO_RESULT_BUSY;
  if (error_)
    return MOJO_RESULT_FAILED_PRECONDITION;

  // See comment in WriteData about ignoring capacity_num_bytes.
  if (*buffer_num_bytes == 0)
    *buffer_num_bytes = options_.capacity_num_bytes;

  two_phase_data_.resize(*buffer_num_bytes);
  *buffer = &two_phase_data_[0];

  // TODO: if buffer_num_bytes.Get() > GetConfiguration().max_message_num_bytes
  // we can construct a MessageInTransit here. But then we need to make
  // MessageInTransit support changing its data size later.

  return MOJO_RESULT_OK;
}

MojoResult DataPipeProducerDispatcher::EndWriteData(
    uint32_t num_bytes_written) {
  base::AutoLock lock(lock_);
  if (port_closed_ || in_transit_)
    return MOJO_RESULT_INVALID_ARGUMENT;

  if (!InTwoPhaseWriteNoLock())
    return MOJO_RESULT_FAILED_PRECONDITION;

  // Note: Allow successful completion of the two-phase write even if the other
  // side has been closed.
  MojoResult rv = MOJO_RESULT_OK;
  if (num_bytes_written > two_phase_data_.size() ||
      num_bytes_written % options_.element_num_bytes != 0) {
    rv = MOJO_RESULT_INVALID_ARGUMENT;
  } else {
    WriteDataIntoMessagesNoLock(&two_phase_data_[0], num_bytes_written);
  }

  // Two-phase write ended even on failure.
  two_phase_data_.clear();
  // If we're now writable, we *became* writable (since we weren't writable
  // during the two-phase write), so awake producer awakables.
  HandleSignalsState new_state = GetHandleSignalsStateNoLock();
  if (new_state.satisfies(MOJO_HANDLE_SIGNAL_WRITABLE))
    awakable_list_.AwakeForStateChange(new_state);

  return rv;
}

HandleSignalsState DataPipeProducerDispatcher::GetHandleSignalsState() const {
  base::AutoLock lock(lock_);
  return GetHandleSignalsStateNoLock();
}

MojoResult DataPipeProducerDispatcher::AddAwakable(
    Awakable* awakable,
    MojoHandleSignals signals,
    uintptr_t context,
    HandleSignalsState* signals_state) {
  base::AutoLock lock(lock_);
  if (in_transit_) {
    if (signals_state)
      *signals_state = HandleSignalsState();
    return MOJO_RESULT_INVALID_ARGUMENT;
  }
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

void DataPipeProducerDispatcher::RemoveAwakable(
    Awakable* awakable,
    HandleSignalsState* signals_state) {
  base::AutoLock lock(lock_);
  if (in_transit_ && signals_state)
    *signals_state = HandleSignalsState();
  else if (signals_state)
    *signals_state = GetHandleSignalsStateNoLock();
  awakable_list_.Remove(awakable);
}

void DataPipeProducerDispatcher::StartSerialize(uint32_t* num_bytes,
                                                uint32_t* num_ports,
                                                uint32_t* num_handles) {
  *num_bytes = sizeof(SerializedState);
  *num_ports = 1;
  *num_handles = 0;
}

bool DataPipeProducerDispatcher::EndSerialize(
    void* destination,
    ports::PortName* ports,
    PlatformHandleVector* platform_handles) {
  SerializedState* state = static_cast<SerializedState*>(destination);
  memcpy(&state->options, &options_, sizeof(MojoCreateDataPipeOptions));

  base::AutoLock lock(lock_);
  ports[0] = port_.name();
  return true;
}

bool DataPipeProducerDispatcher::BeginTransit() {
  base::AutoLock lock(lock_);
  if (in_transit_)
    return false;
  in_transit_ = !InTwoPhaseWriteNoLock();
  return in_transit_;
}

void DataPipeProducerDispatcher::CompleteTransitAndClose() {
  base::AutoLock lock(lock_);
  port_transferred_ = true;
  in_transit_ = false;
  CloseNoLock();
}

void DataPipeProducerDispatcher::CancelTransit() {
  base::AutoLock lock(lock_);
  in_transit_ = false;

  awakable_list_.AwakeForStateChange(GetHandleSignalsStateNoLock());
}

// static
scoped_refptr<DataPipeProducerDispatcher>
DataPipeProducerDispatcher::Deserialize(const void* data,
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

  NodeController* node_controller = internal::g_core->GetNodeController();
  ports::PortRef port;
  if (node_controller->node()->GetPort(ports[0], &port) != ports::OK)
    return nullptr;

  return new DataPipeProducerDispatcher(node_controller, port, state->options);
}

DataPipeProducerDispatcher::~DataPipeProducerDispatcher() {
  DCHECK(port_closed_ && !in_transit_);
}

MojoResult DataPipeProducerDispatcher::CloseNoLock() {
  lock_.AssertAcquired();
  if (port_closed_ || in_transit_)
    return MOJO_RESULT_INVALID_ARGUMENT;
  port_closed_ = true;

  if (!port_transferred_)
    node_controller_->node()->ClosePort(port_);
  awakable_list_.CancelAll();

  return MOJO_RESULT_OK;
}

HandleSignalsState DataPipeProducerDispatcher::GetHandleSignalsStateNoLock()
    const {
  lock_.AssertAcquired();
  HandleSignalsState rv;
  if (!error_) {
    if (!InTwoPhaseWriteNoLock())
      rv.satisfied_signals |= MOJO_HANDLE_SIGNAL_WRITABLE;
    rv.satisfiable_signals |= MOJO_HANDLE_SIGNAL_WRITABLE;
  } else {
    rv.satisfied_signals |= MOJO_HANDLE_SIGNAL_PEER_CLOSED;
  }
  rv.satisfiable_signals |= MOJO_HANDLE_SIGNAL_PEER_CLOSED;
  return rv;
}

bool DataPipeProducerDispatcher::WriteDataIntoMessagesNoLock(
    const void* elements,
    uint32_t num_bytes) {
  // The maximum amount of data to send per message (make it a multiple of the
  // element size.
  size_t max_message_num_bytes = GetConfiguration().max_message_num_bytes;
  max_message_num_bytes -= max_message_num_bytes % options_.element_num_bytes;
  DCHECK_GT(max_message_num_bytes, 0u);

  uint32_t offset = 0;
  while (offset < num_bytes) {
    uint32_t message_num_bytes =
        std::min(static_cast<uint32_t>(max_message_num_bytes),
                 num_bytes - offset);

    scoped_ptr<PortsMessage> message =
        node_controller_->AllocMessage(num_bytes, 0);
    memcpy(message->mutable_payload_bytes(),
           static_cast<const char*>(elements) + offset, message_num_bytes);
    int rv = node_controller_->SendMessage(port_, &message);
    if (rv != ports::OK) {
      error_ = true;
      return false;
    }

    offset += message_num_bytes;
  }

  return true;
}

bool DataPipeProducerDispatcher::InTwoPhaseWriteNoLock() const {
  return !two_phase_data_.empty();
}

void DataPipeProducerDispatcher::OnPortStatusChanged() {
  base::AutoLock lock(lock_);

  ports::PortStatus port_status;
  if (node_controller_->node()->GetStatus(port_, &port_status) != ports::OK ||
      port_status.peer_closed) {
    error_ = true;
  }

  if (port_status.has_messages) {
    LOG(ERROR) << "Data pipe producers should never receive messages.";
    error_ = true;
  }

  awakable_list_.AwakeForStateChange(GetHandleSignalsStateNoLock());
}

}  // namespace edk
}  // namespace mojo
