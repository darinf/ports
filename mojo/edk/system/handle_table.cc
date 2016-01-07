// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/edk/system/handle_table.h"

#include <limits>

namespace mojo {
namespace edk {

HandleTable::HandleTable() {}

HandleTable::~HandleTable() {}

MojoHandle HandleTable::AddDispatcher(scoped_refptr<Dispatcher> dispatcher) {
  // Oops, we're out of handles.
  if (next_available_handle_ == MOJO_HANDLE_INVALID)
    return MOJO_HANDLE_INVALID;

  MojoHandle handle = next_available_handle_++;
  auto result = handles_.insert(std::make_pair(handle, Entry(dispatcher)));
  DCHECK(result.second);

  return handle;
}

bool HandleTable::AddDispatchersFromTransit(
    const std::vector<Dispatcher::DispatcherInTransit>& dispatchers,
    MojoHandle* handles) {
  // Oops, we're out of handles.
  if (next_available_handle_ == MOJO_HANDLE_INVALID)
    return false;

  DCHECK_LE(dispatchers.size(), std::numeric_limits<uint32_t>::max());
  // If this insertion would cause handle overflow, we're out of handles.
  if (next_available_handle_ + dispatchers.size() < next_available_handle_)
    return false;

  for (size_t i = 0; i < dispatchers.size(); ++i) {
    MojoHandle handle = next_available_handle_++;
    auto result = handles_.insert(
        std::make_pair(handle, Entry(dispatchers[i].dispatcher)));
    DCHECK(result.second);
    handles[i] = handle;
  }

  return true;
}

scoped_refptr<Dispatcher> HandleTable::GetDispatcher(MojoHandle handle) const {
  DCHECK_NE(handle, MOJO_HANDLE_INVALID);

  auto it = handles_.find(handle);
  if (it == handles_.end())
    return nullptr;
  return it->second.dispatcher;
}

MojoResult HandleTable::GetAndRemoveDispatcher(
    MojoHandle handle,
    scoped_refptr<Dispatcher>* dispatcher) {
  DCHECK_NE(handle, MOJO_HANDLE_INVALID);

  auto it = handles_.find(handle);
  if (it == handles_.end())
    return MOJO_RESULT_INVALID_ARGUMENT;
  if (it->second.busy)
    return MOJO_RESULT_BUSY;

  *dispatcher = it->second.dispatcher;
  handles_.erase(it);
  return MOJO_RESULT_OK;
}

MojoResult HandleTable::BeginTransit(
    const MojoHandle* handles,
    uint32_t num_handles,
    std::vector<Dispatcher::DispatcherInTransit>* dispatchers) {
  dispatchers->resize(num_handles);
  for (size_t i = 0; i < num_handles; ++i) {
    auto it = handles_.find(handles[i]);
    if (it == handles_.end())
      return MOJO_RESULT_INVALID_ARGUMENT;
    if (it->second.busy)
      return MOJO_RESULT_BUSY;
    it->second.busy = true;

    Dispatcher::DispatcherInTransit& d = dispatchers->at(i);
    d.local_handle = handles[i];
    d.dispatcher = it->second.dispatcher;
    d.dispatcher->BeginTransit();
  }
  return MOJO_RESULT_OK;
}

void HandleTable::CompleteTransit(
    const std::vector<Dispatcher::DispatcherInTransit>& dispatchers) {
  for (const auto& dispatcher : dispatchers) {
    auto it = handles_.find(dispatcher.local_handle);
    DCHECK(it != handles_.end() && it->second.busy);
    handles_.erase(it);
    dispatcher.dispatcher->CompleteTransit();
  }
}

void HandleTable::CancelTransit(
    const std::vector<Dispatcher::DispatcherInTransit>& dispatchers) {
  for (const auto& dispatcher : dispatchers) {
    auto it = handles_.find(dispatcher.local_handle);
    DCHECK(it != handles_.end() && it->second.busy);
    it->second.busy = false;
    dispatcher.dispatcher->CancelTransit();
  }
}

HandleTable::Entry::Entry() {}

HandleTable::Entry::Entry(scoped_refptr<Dispatcher> dispatcher)
    : dispatcher(dispatcher) {}

HandleTable::Entry::~Entry() {}

}  // namespace edk
}  // namespace mojo
