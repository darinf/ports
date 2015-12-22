// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PORTS_MOJO_SYSTEM_HANDLE_TABLE_H_
#define PORTS_MOJO_SYSTEM_HANDLE_TABLE_H_

#include <vector>

#include "base/containers/hash_tables.h"
#include "base/macros.h"
#include "mojo/public/c/system/types.h"
#include "ports/mojo_system/dispatcher.h"

namespace mojo {
namespace edk {

class HandleTable {
 public:
  HandleTable();
  ~HandleTable();

  MojoHandle AddDispatcher(scoped_refptr<Dispatcher> dispatcher);

  // Inserts multiple dispatchers received from message transit, populating
  // |handles| with their newly allocated handles. Returns |true| on success.
  bool AddDispatchersFromTransit(
      const std::vector<Dispatcher::DispatcherInTransit>& dispatchers,
      MojoHandle* handles);

  scoped_refptr<Dispatcher> GetDispatcher(MojoHandle handle) const;
  MojoResult GetAndRemoveDispatcher(MojoHandle,
                                    scoped_refptr<Dispatcher>* dispatcher);

  // Marks handles as busy and populates |dispatchers|. Returns MOJO_RESULT_BUSY
  // if any of the handles are already in transit; MOJO_RESULT_INVALID_ARGUMENT
  // if any of the handles are invalid; or MOJO_RESULT_OK if successful.
  MojoResult BeginTransit(
      const MojoHandle* handles,
      uint32_t num_handles,
      std::vector<Dispatcher::DispatcherInTransit>* dispatchers);

  void CompleteTransit(
      const std::vector<Dispatcher::DispatcherInTransit>& dispatchers);
  void CancelTransit(
      const std::vector<Dispatcher::DispatcherInTransit>& dispatchers);

 private:
  struct Entry {
   Entry();
   explicit Entry(scoped_refptr<Dispatcher> dispatcher);
   ~Entry();

   scoped_refptr<Dispatcher> dispatcher;
   bool busy = false;
  };

  using HandleMap = base::hash_map<MojoHandle, Entry>;

  HandleMap handles_;

  uint32_t next_available_handle_ = 1;

  DISALLOW_COPY_AND_ASSIGN(HandleTable);
};

}  // namespace edk
}  // namespace mojo

#endif  // PORTS_MOJO_SYSTEM_HANDLE_TABLE_H_
