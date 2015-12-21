// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ports/mojo_system/channel.h"

#include <errno.h>
#include <sys/uio.h>

#include <algorithm>
#include <deque>
#include <vector>

#include "base/bind.h"
#include "base/location.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/message_loop/message_pump_libevent.h"
#include "base/synchronization/lock.h"
#include "base/task_runner.h"
#include "mojo/edk/embedder/platform_channel_utils_posix.h"
#include "mojo/edk/embedder/platform_handle_vector.h"

namespace mojo {
namespace edk {

namespace {

const size_t kReadBufferSize = 4096;

class ChannelPosix : public Channel, public base::MessagePumpLibevent::Watcher {
 public:
  ChannelPosix(Delegate* delegate,
               ScopedPlatformHandle handle,
               scoped_refptr<base::TaskRunner> io_task_runner)
      : delegate_(delegate),
        handle_(std::move(handle)),
        io_task_runner_(io_task_runner),
        read_buffer_(kReadBufferSize) {
    io_task_runner_->PostTask(
        FROM_HERE, base::Bind(&ChannelPosix::StartOnIOThread, this));
  }

  // Safe to call from any thread.
  void Write(OutgoingMessagePtr message) override {
    base::AutoLock lock(write_lock_);
    bool wait_for_write = outgoing_messages_.empty();
    outgoing_messages_.emplace_back(std::move(message));
    if (wait_for_write) {
      io_task_runner_->PostTask(
          FROM_HERE, base::Bind(&ChannelPosix::WaitForWriteOnIOThread, this));
    }
  }

 private:
  ~ChannelPosix() override {}

  void StartOnIOThread() {
    DCHECK(!read_watcher_);
    DCHECK(!write_watcher_);
    read_watcher_.reset(new base::MessagePumpLibevent::FileDescriptorWatcher);
    write_watcher_.reset(new base::MessagePumpLibevent::FileDescriptorWatcher);
    base::MessageLoopForIO::current()->WatchFileDescriptor(
        handle_.get().handle, true /* persistent */,
        base::MessageLoopForIO::WATCH_READ, read_watcher_.get(), this);
  }

  void WaitForWriteOnIOThread() {
    DCHECK(write_watcher_);
    base::MessageLoopForIO::current()->WatchFileDescriptor(
        handle_.get().handle, false /* persistent */,
        base::MessageLoopForIO::WATCH_WRITE, write_watcher_.get(), this);
  }

  void ShutDownOnIOThread() {
    read_watcher_.reset();
    write_watcher_.reset();
    handle_.reset();
    delegate_->OnChannelError();
  }

  // base::MessagePumpLibevent::Watcher:
  void OnFileCanReadWithoutBlocking(int fd) override {
    CHECK_EQ(fd, handle_.get().handle);
    std::deque<PlatformHandle> handles;
    ssize_t read_result = PlatformChannelRecvmsg(
        handle_.get(), read_buffer_.data(), read_buffer_.size(), &handles);

    if (read_result > 0) {
      size_t num_bytes = static_cast<size_t>(read_result);
      ScopedPlatformHandleVectorPtr scoped_handles(
          new PlatformHandleVector(handles.size()));
      std::copy(handles.begin(), handles.end(), scoped_handles->begin());
      IncomingMessage message(
          read_buffer_.data(), num_bytes, std::move(scoped_handles));
      delegate_->OnChannelRead(&message);
    } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
      ShutDownOnIOThread();
    }
  }

  void OnFileCanWriteWithoutBlocking(int fd) override {
    std::deque<OutgoingMessagePtr> messages;
    {
      base::AutoLock lock(write_lock_);
      std::swap(outgoing_messages_, messages);
    }

    // TODO: Send a batch of iovecs when possible.
    while (!messages.empty()) {
      OutgoingMessagePtr message = std::move(messages.front());
      iovec iov = { const_cast<void*>(message->data()), message->num_bytes() };
      ssize_t result;
      if (message->num_handles()) {
        result = PlatformChannelSendmsgWithHandles(
            handle_.get(), &iov, 1, message->handles(), message->num_handles());
      } else {
        result = PlatformChannelWritev(handle_.get(), &iov, 1);
      }

      if (result >= 0) {
        messages.pop_front();
      } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
        ShutDownOnIOThread();
        return;
      } else {
        messages.front() = std::move(message);
        break;
      }
    }

    if (!messages.empty()) {
      // Put back any remaining messages if necessary, preserving order.
      base::AutoLock lock(write_lock_);
      std::move(messages.begin(), messages.end(),
          std::front_inserter(outgoing_messages_));
    }
  }

  Delegate* delegate_;
  ScopedPlatformHandle handle_;
  scoped_refptr<base::TaskRunner> io_task_runner_;

  // These watchers must only be accessed on the IO thread.
  scoped_ptr<base::MessagePumpLibevent::FileDescriptorWatcher> read_watcher_;
  scoped_ptr<base::MessagePumpLibevent::FileDescriptorWatcher> write_watcher_;

  // Must only be accessed on the IO thread.
  std::vector<char> read_buffer_;

  // Protects |outgoing_messages_|.
  base::Lock write_lock_;
  std::deque<OutgoingMessagePtr> outgoing_messages_;

  DISALLOW_COPY_AND_ASSIGN(ChannelPosix);
};

}  // namespace

// static
scoped_refptr<Channel> Channel::Create(
    Delegate* delegate,
    ScopedPlatformHandle platform_handle,
    scoped_refptr<base::TaskRunner> io_task_runner) {
  return new ChannelPosix(delegate, std::move(platform_handle), io_task_runner);
}

}  // namespace edk
}  // namespace mojo
