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

// A view over an OutgoingMessage object. The write queue uses these since
// large messages may need to be sent in chunks.
class OutgoingMessageView {
 public:
  OutgoingMessageView(Channel::OutgoingMessagePtr message,
                      size_t offset)
      : message_(std::move(message)),
        offset_(offset),
        handles_(message_->TakeHandles()) {
    DCHECK_GT(message_->data_num_bytes(), offset_);
  }

  ~OutgoingMessageView() {}

  const void* data() const {
    return static_cast<const char*>(message_->data()) + offset_;
  }

  size_t data_num_bytes() const { return message_->data_num_bytes() - offset_; }

  size_t num_handles() const { return handles_ ? handles_->size() : 0; }

  PlatformHandle* handles() {
    return static_cast<PlatformHandle*>(handles_->data());
  }

  Channel::OutgoingMessagePtr TakeMessage() { return std::move(message_); }

 private:
  Channel::OutgoingMessagePtr message_;
  const size_t offset_;
  ScopedPlatformHandleVectorPtr handles_;

  DISALLOW_COPY_AND_ASSIGN(OutgoingMessageView);
};

using OutgoingMessageViewPtr = scoped_ptr<OutgoingMessageView>;

class ChannelPosix : public Channel,
                     public base::MessageLoop::DestructionObserver,
                     public base::MessagePumpLibevent::Watcher {
 public:
  ChannelPosix(Delegate* delegate,
               ScopedPlatformHandle handle,
               scoped_refptr<base::TaskRunner> io_task_runner)
      : Channel(delegate),
        self_(this),
        handle_(std::move(handle)),
        io_task_runner_(io_task_runner) {
  }

  void Start() override {
    io_task_runner_->PostTask(
        FROM_HERE, base::Bind(&ChannelPosix::StartOnIOThread, this));
  }

  void ShutDown() override {
    io_task_runner_->PostTask(
        FROM_HERE, base::Bind(&ChannelPosix::ShutDownOnIOThread, this));
  }

  void Write(OutgoingMessagePtr message) override {
    base::AutoLock lock(write_lock_);
    bool wait_for_write = outgoing_messages_.empty();
    outgoing_messages_.emplace_back(
        new OutgoingMessageView(std::move(message), 0));
    if (wait_for_write) {
      io_task_runner_->PostTask(
          FROM_HERE, base::Bind(&ChannelPosix::WaitForWriteOnIOThread, this));
    }
  }

  ScopedPlatformHandleVectorPtr GetReadPlatformHandlesNoLock(
      size_t num_handles) override {
    read_lock().AssertAcquired();
    if (incoming_platform_handles_.size() < num_handles)
      return nullptr;
    ScopedPlatformHandleVectorPtr handles(
        new PlatformHandleVector(num_handles));
    for (size_t i = 0; i < num_handles; ++i) {
      (*handles)[i] = incoming_platform_handles_.front();
      incoming_platform_handles_.pop_front();
    }
    return handles;
  }

 private:
  ~ChannelPosix() override {
    for (auto handle : incoming_platform_handles_)
      handle.CloseIfNecessary();
  }

  void StartOnIOThread() {
    DCHECK(!read_watcher_);
    DCHECK(!write_watcher_);
    read_watcher_.reset(new base::MessagePumpLibevent::FileDescriptorWatcher);
    write_watcher_.reset(new base::MessagePumpLibevent::FileDescriptorWatcher);
    base::MessageLoopForIO::current()->WatchFileDescriptor(
        handle_.get().handle, true /* persistent */,
        base::MessageLoopForIO::WATCH_READ, read_watcher_.get(), this);
    base::MessageLoop::current()->AddDestructionObserver(this);
  }

  void WaitForWriteOnIOThread() {
    base::AutoLock lock(write_lock_);
    if (pending_write_)
      return;
    if (!write_watcher_)
      return;
    pending_write_ = true;
    base::MessageLoopForIO::current()->WatchFileDescriptor(
        handle_.get().handle, false /* persistent */,
        base::MessageLoopForIO::WATCH_WRITE, write_watcher_.get(), this);
  }

  void ShutDownOnIOThread() {
    read_watcher_.reset();
    write_watcher_.reset();
    handle_.reset();
  }

  // base::MessageLoop::DestructionObserver:
  void WillDestroyCurrentMessageLoop() override {
    DCHECK(io_task_runner_->RunsTasksOnCurrentThread());
    if (self_) {
      ShutDownOnIOThread();
      self_ = nullptr;
    }
  }

  // base::MessagePumpLibevent::Watcher:
  void OnFileCanReadWithoutBlocking(int fd) override {
    CHECK_EQ(fd, handle_.get().handle);
    base::AutoLock lock(read_lock());

    size_t buffer_capacity;
    char* buffer = GetReadBuffer(&buffer_capacity);
    ssize_t read_result = PlatformChannelRecvmsg(
        handle_.get(), buffer, buffer_capacity, &incoming_platform_handles_);

    if (read_result > 0) {
      OnReadCompleteNoLock(static_cast<size_t>(read_result));
    } else if (read_result == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
      ShutDownOnIOThread();
      OnError();
    }
  }

  void OnFileCanWriteWithoutBlocking(int fd) override {
    std::deque<OutgoingMessageViewPtr> messages;
    {
      base::AutoLock lock(write_lock_);
      std::swap(outgoing_messages_, messages);
      pending_write_ = false;
    }

    // TODO: Send a batch of iovecs when possible.
    while (!messages.empty()) {
      OutgoingMessageViewPtr message_view = std::move(messages.front());
      iovec iov = {
        const_cast<void*>(message_view->data()),
        message_view->data_num_bytes()
      };
      ssize_t result;
      if (message_view->num_handles()) {
        // TODO: Handle lots of handles.
        result = PlatformChannelSendmsgWithHandles(
            handle_.get(), &iov, 1, message_view->handles(),
            message_view->num_handles());
      } else {
        result = PlatformChannelWritev(handle_.get(), &iov, 1);
      }

      if (result >= 0) {
        size_t bytes_written = static_cast<size_t>(result);
        if (bytes_written < message_view->data_num_bytes()) {
          message_view.reset(new OutgoingMessageView(
              message_view->TakeMessage(), bytes_written));
          messages.front() = std::move(message_view);
        } else {
          messages.pop_front();
        }
      } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
        ShutDownOnIOThread();
        OnError();
        return;
      } else {
        // Need to try again!
        messages.front() = std::move(message_view);
        WaitForWriteOnIOThread();
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

  // Keeps the Channel alive at least until explicit shutdown on the IO thread.
  scoped_refptr<Channel> self_;

  ScopedPlatformHandle handle_;
  scoped_refptr<base::TaskRunner> io_task_runner_;

  // These watchers must only be accessed on the IO thread.
  scoped_ptr<base::MessagePumpLibevent::FileDescriptorWatcher> read_watcher_;
  scoped_ptr<base::MessagePumpLibevent::FileDescriptorWatcher> write_watcher_;

  std::deque<PlatformHandle> incoming_platform_handles_;

  // Protects |pending_write_| and |outgoing_messages_|.
  base::Lock write_lock_;
  bool pending_write_ = false;
  std::deque<OutgoingMessageViewPtr> outgoing_messages_;

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
