// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/edk/system/channel.h"

#include <windows.h>

#include <algorithm>
#include <deque>

#include "base/bind.h"
#include "base/location.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/synchronization/lock.h"
#include "base/task_runner.h"
#include "mojo/edk/embedder/platform_handle_vector.h"

namespace mojo {
namespace edk {

namespace {

const size_t kMaxBatchReadCapacity = 256 * 1024;

// A view over a Channel::Message object. The write queue uses these since
// large messages may need to be sent in chunks.
class MessageView {
 public:
  // Owns |message|. |offset| indexes the first unsent byte in the message.
  MessageView(Channel::MessagePtr message, size_t offset)
      : message_(std::move(message)),
        offset_(offset),
        handles_(message_->TakeHandles()) {
    DCHECK_GT(message_->data_num_bytes(), offset_);
  }

  MessageView(MessageView&& other) { *this = std::move(other); }

  MessageView& operator=(MessageView&& other) {
    message_ = std::move(other.message_);
    offset_ = other.offset_;
    handles_ = std::move(other.handles_);
    return *this;
  }

  ~MessageView() {}

  const void* data() const {
    return static_cast<const char*>(message_->data()) + offset_;
  }

  size_t data_num_bytes() const { return message_->data_num_bytes() - offset_; }

  size_t data_offset() const { return offset_; }
  void advance_data_offset(size_t num_bytes) {
    DCHECK_GE(message_->data_num_bytes(), offset_ + num_bytes);
    offset_ += num_bytes;
  }

  ScopedPlatformHandleVectorPtr TakeHandles() { return std::move(handles_); }
  Channel::MessagePtr TakeMessage() { return std::move(message_); }

 private:
  Channel::MessagePtr message_;
  size_t offset_;
  ScopedPlatformHandleVectorPtr handles_;

  DISALLOW_COPY_AND_ASSIGN(MessageView);
};

class ChannelWin : public Channel,
                   public base::MessageLoop::DestructionObserver,
                   public base::MessageLoopForIO::IOHandler {
 public:
  ChannelWin(Delegate* delegate,
             ScopedPlatformHandle handle,
             scoped_refptr<base::TaskRunner> io_task_runner)
      : Channel(delegate),
        self_(this),
        handle_(std::move(handle)),
        io_task_runner_(io_task_runner) {
    memset(&read_context_, 0, sizeof(read_context_));
    read_context_.handler = this;

    memset(&write_context_, 0, sizeof(write_context_));
    write_context_.handler = this;
  }

  void Start() override {
    io_task_runner_->PostTask(
        FROM_HERE, base::Bind(&ChannelWin::StartOnIOThread, this));
  }

  void ShutDownImpl() override {
    if (io_task_runner_->RunsTasksOnCurrentThread()) {
      ShutDownOnIOThread();
    } else {
      io_task_runner_->PostTask(
          FROM_HERE, base::Bind(&ChannelWin::ShutDownOnIOThread, this));
    }
  }

  void Write(MessagePtr message) override {
    bool write_error = false;
    {
      base::AutoLock lock(write_lock_);
      if (reject_writes_)
        return;
      if (outgoing_messages_.empty()) {
        if (!WriteNoLock(MessageView(std::move(message), 0)))
          reject_writes_ = write_error = true;
      } else {
        outgoing_messages_.emplace_back(std::move(message), 0);
      }
    }
    if (write_error)
      OnError();
  }

  ScopedPlatformHandleVectorPtr GetReadPlatformHandles(
      size_t num_handles) override {
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
  // TODO: There is a race here. It is not certain that the last reference
  // will be dropped on the IO thread. We should modify Channel to enforce
  // this via the extra param to RefCountedThreadSafe.
  ~ChannelWin() override {
    DCHECK(io_task_runner_->RunsTasksOnCurrentThread());
    base::MessageLoop::current()->RemoveDestructionObserver(this);
    for (auto handle : incoming_platform_handles_)
      handle.CloseIfNecessary();
  }

  void StartOnIOThread() {
    base::MessageLoop::current()->AddDestructionObserver(this);
    base::MessageLoopForIO::current()->RegisterIOHandler(
        handle_.get().handle, this);
    ReadMore(0);
  }

  void ShutDownOnIOThread() {
    // TODO: cancel overlapped IO

    handle_.reset();

    // May destroy the |this| if it was the last reference.
    self_ = nullptr;
  }

  // base::MessageLoop::DestructionObserver:
  void WillDestroyCurrentMessageLoop() override {
    DCHECK(io_task_runner_->RunsTasksOnCurrentThread());
    if (self_)
      ShutDownOnIOThread();
  }

  // base::MessageLoop::IOHandler:
  void OnIOCompleted(base::MessageLoopForIO::IOContext* context,
                     DWORD bytes_transfered,
                     DWORD error) override {
    if (error != ERROR_SUCCESS) {
      OnError();
    } else if (context == &read_context_) {
      OnReadDone(static_cast<size_t>(bytes_transfered));
    } else {
      CHECK(context == &write_context_);
      OnWriteDone(static_cast<size_t>(bytes_transfered));
    }
  }

  void OnReadDone(size_t bytes_read) {
    if (bytes_read > 0) {
      size_t next_read_size = 0;
      if (OnReadComplete(bytes_read, &next_read_size)) {
        ReadMore(next_read_size);
      } else {
        OnError();
      }
    } else if (bytes_read == 0) {
      OnError();
    }
  }

  void OnWriteDone(size_t bytes_written) {
    if (bytes_written == 0)
      return;

    bool write_error = false;
    {
      base::AutoLock lock(write_lock_);

      // How can this be empty at this point?
      if (outgoing_messages_.empty())
        return;

      MessageView& message_view = outgoing_messages_.front();
      message_view.advance_data_offset(bytes_written);
      if (message_view.data_offset() == message_view.data_num_bytes())
        outgoing_messages_.pop_front();

      if (!FlushOutgoingMessagesNoLock())
        reject_writes_ = write_error = true;
    }
    if (write_error)
      OnError();
  }

  void ReadMore(size_t next_read_size) {
    bool read_error = false;
    {
      size_t buffer_capacity = 0;
      size_t total_bytes_read = 0;
      size_t bytes_read = 0;
      do {
        buffer_capacity = next_read_size;
        char* buffer = GetReadBuffer(&buffer_capacity);
        DCHECK_GT(buffer_capacity, 0u);

        DWORD read_result = 0;
        BOOL ok = ReadFile(handle_.get().handle,
                           buffer,
                           static_cast<DWORD>(buffer_capacity),
                           &read_result,
                           &read_context_.overlapped);

        if (ok && read_result > 0) {
          bytes_read = static_cast<size_t>(read_result);
          total_bytes_read += bytes_read;
          if (!OnReadComplete(bytes_read, &next_read_size)) {
            read_error = true;
            break;
          }
        } else if ((ok && read_result == 0) ||
                   (GetLastError() != ERROR_IO_PENDING)) {
          read_error = true;
          break;
        }
      } while (bytes_read == buffer_capacity &&
               total_bytes_read < kMaxBatchReadCapacity &&
               next_read_size > 0);
    }
    if (read_error)
      OnError();
  }

  // Attempts to write a message directly to the channel. If the full message
  // cannot be written, it's queued and a wait is initiated to write the message
  // ASAP on the I/O thread.
  bool WriteNoLock(MessageView message_view) {
    size_t bytes_written = 0;
    do {
      message_view.advance_data_offset(bytes_written);

      ScopedPlatformHandleVectorPtr handles = message_view.TakeHandles();
      if (handles && handles->size()) {
        DCHECK(false) << "not implemented";
        return false;
      }

      DWORD write_result = 0;
      BOOL ok = WriteFile(handle_.get().handle,
                          message_view.data(),
                          static_cast<DWORD>(message_view.data_num_bytes()),
                          &write_result,
                          &write_context_.overlapped);
      if (!ok) {
        if (GetLastError() != ERROR_IO_PENDING)
          return false;
        outgoing_messages_.emplace_back(std::move(message_view));
        return true;
      }

      bytes_written = static_cast<size_t>(write_result);
    } while (bytes_written < message_view.data_num_bytes());
    return true;
  }

  bool FlushOutgoingMessagesNoLock() {
    std::deque<MessageView> messages;
    std::swap(outgoing_messages_, messages);

    while (!messages.empty()) {
      if (!WriteNoLock(std::move(messages.front())))
        return false;

      messages.pop_front();
      if (!outgoing_messages_.empty()) {
        // The message was requeued by WriteNoLock(), so we have to wait for
        // pipe to become writable again. Repopulate the message queue and exit.
        DCHECK_EQ(outgoing_messages_.size(), 1u);
        MessageView message_view = std::move(outgoing_messages_.front());
        std::swap(messages, outgoing_messages_);
        outgoing_messages_.push_front(std::move(message_view));
        return true;
      }
    }

    return true;
  }

  // Keeps the Channel alive at least until explicit shutdown on the IO thread.
  scoped_refptr<Channel> self_;

  ScopedPlatformHandle handle_;
  scoped_refptr<base::TaskRunner> io_task_runner_;

  base::MessageLoopForIO::IOContext read_context_;
  base::MessageLoopForIO::IOContext write_context_;

  std::deque<PlatformHandle> incoming_platform_handles_;

  // Protects |reject_writes_| and |outgoing_messages_|.
  base::Lock write_lock_;

  bool reject_writes_ = false;
  std::deque<MessageView> outgoing_messages_;

  DISALLOW_COPY_AND_ASSIGN(ChannelWin);
};

}  // namespace

// static
scoped_refptr<Channel> Channel::Create(
    Delegate* delegate,
    ScopedPlatformHandle platform_handle,
    scoped_refptr<base::TaskRunner> io_task_runner) {
  return new ChannelWin(delegate, std::move(platform_handle), io_task_runner);
}

}  // namespace edk
}  // namespace mojo
