// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ports/mojo_system/channel.h"

#include <sys/uio.h>

#include "base/macros.h"
#include "mojo/edk/embedder/platform_channel_utils_posix.h"

namespace mojo {
namespace edk {

namespace {

// TODO: More careful IO.
class ChannelPosix : public Channel {
 public:
  ChannelPosix(ScopedPlatformHandle platform_handle)
      : platform_handle_(std::move(platform_handle)) {}

  ~ChannelPosix() override {}

  ssize_t WriteWithHandles(const void* bytes,
                           size_t num_bytes,
                           PlatformHandle* handles,
                           size_t num_handles) override {
    iovec iov = { const_cast<void*>(bytes), num_bytes };
    if (handles) {
      return PlatformChannelSendmsgWithHandles(
          platform_handle_.get(), &iov, 1, handles, num_handles);
    }
    return PlatformChannelWritev(platform_handle_.get(), &iov, 1);
  }

  ssize_t Read(void* bytes, size_t num_bytes,
               std::deque<PlatformHandle>* handles) override {
    return PlatformChannelRecvmsg(
        platform_handle_.get(), bytes, num_bytes, handles);
  }

 private:
  ScopedPlatformHandle platform_handle_;

  DISALLOW_COPY_AND_ASSIGN(ChannelPosix);
};

}  // namespace

// static
scoped_ptr<Channel> Channel::Create(ScopedPlatformHandle platform_handle) {
  return make_scoped_ptr(new ChannelPosix(std::move(platform_handle)));
}

}  // namespace edk
}  // namespace mojo
