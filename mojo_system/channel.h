// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PORTS_MOJO_SYSTEM_CHANNEL_H_
#define PORTS_MOJO_SYSTEM_CHANNEL_H_

#include <deque>

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "mojo/edk/embedder/scoped_platform_handle.h"

namespace mojo {
namespace edk {

class Channel {
 public:
  static scoped_ptr<Channel> Create(ScopedPlatformHandle platform_handle);

  virtual ~Channel();

  ssize_t Write(const void* bytes, size_t num_bytes) {
    return WriteWithHandles(bytes, num_bytes, nullptr, 0);
  }

  virtual ssize_t WriteWithHandles(const void* bytes,
                                   size_t num_bytes,
                                   PlatformHandle* handles,
                                   size_t num_handles) = 0;

  virtual ssize_t Read(void* bytes, size_t num_bytes,
                       std::deque<PlatformHandle>* handles) = 0;

 protected:
  Channel();

 private:
  DISALLOW_COPY_AND_ASSIGN(Channel);
};

}  // namespace edk
}  // namespace mojo

#endif  // PORTS_MOJO_SYSTEM_CHANNEL_H_
