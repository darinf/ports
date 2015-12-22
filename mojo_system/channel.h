// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PORTS_MOJO_SYSTEM_CHANNEL_H_
#define PORTS_MOJO_SYSTEM_CHANNEL_H_

#include <vector>

#include "base/logging.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/synchronization/lock.h"
#include "base/task_runner.h"
#include "mojo/edk/embedder/platform_handle_vector.h"
#include "mojo/edk/embedder/scoped_platform_handle.h"

namespace mojo {
namespace edk {

const size_t kChannelMessageAlignment = 8;

// Channel provides a thread-safe interface to read and write arbitrary
// delimited messages over an underlying IO channel, optionally transferring
// one or more platform handles in the process.
class Channel : public base::RefCountedThreadSafe<Channel> {
 public:
  // All messages sent over a Channel start with this header.
  struct MessageHeader {
    // Message size in bytes, including the header.
    uint32_t num_bytes;

    // Number of attached handles.
    uint16_t num_handles;

    // Zero
    uint16_t padding;
  };

  static_assert(sizeof(MessageHeader) % kChannelMessageAlignment == 0,
      "MessageHeader size must be aligned to kChannelMessageAlignment bytes.");

  // A view over some message data which was read from the channel.
  struct IncomingMessage {
    // Doesn't own |data|. Does own |handles| and can transfer ownership.
    IncomingMessage(const void* data,
                    ScopedPlatformHandleVectorPtr handles);
    ~IncomingMessage();

    bool IsValid() const;

    const void* payload() const { return &header_[1]; }

    size_t payload_size() const {
      return header_->num_bytes - sizeof(MessageHeader);
    }

    size_t num_handles() const { return header_->num_handles; }

    ScopedPlatformHandleVectorPtr TakeHandles() { return std::move(handles_); }

   private:
    const MessageHeader* header_;
    ScopedPlatformHandleVectorPtr handles_;

    DISALLOW_COPY_AND_ASSIGN(IncomingMessage);
  };

  // A message to be written to the channel.
  struct OutgoingMessage {
    // Copies |payload| and takes ownership of |handles|. If |payload| is
    // null this simply allocates and zeroes a payload of |payload_size| bytes.
    OutgoingMessage(const void* payload,
                    size_t payload_size,
                    ScopedPlatformHandleVectorPtr handles);
    ~OutgoingMessage();

    const void* data() const { return header_; }
    size_t data_num_bytes() const { return data_.size(); }

    void* mutable_payload() { return &header_[1]; }
    const void* payload() const { return &header_[1]; }

    size_t payload_size() const {
      return data_.size() - sizeof(MessageHeader);
    }

    size_t num_handles() const { return header_->num_handles; }

    PlatformHandle* handles() {
      DCHECK(handles_);
      return static_cast<PlatformHandle*>(handles_->data());
    }

    ScopedPlatformHandleVectorPtr TakeHandles() { return std::move(handles_); }

   private:
    std::vector<char> data_;
    MessageHeader* const header_;
    ScopedPlatformHandleVectorPtr handles_;

    DISALLOW_COPY_AND_ASSIGN(OutgoingMessage);
  };

  using OutgoingMessagePtr = scoped_ptr<OutgoingMessage>;

  // Delegate methods are called from the IO task runner with which the Channel
  // was created (see Channel::Create).
  class Delegate {
   public:
    virtual ~Delegate() {}

    // A message has been received!
    virtual void OnChannelRead(IncomingMessage* message) = 0;

    // A fatal error has occurred; the Channel has ceased to function.
    virtual void OnChannelError() = 0;
  };

  class ReadBuffer {
   public:
    ReadBuffer();
    ~ReadBuffer();

    void GetBuffer(char** addr, size_t* bytes_to_read);

   private:
    friend class Channel;

    std::vector<char> data_;
    size_t num_valid_bytes_ = 0;

    DISALLOW_COPY_AND_ASSIGN(ReadBuffer);
  };

  // Creates a new Channel around a |platform_handle|, taking ownership of the
  // handle. All IO on the handle will be performed on |io_task_runner|.
  static scoped_refptr<Channel> Create(
      Delegate* delegate,
      ScopedPlatformHandle platform_handle,
      scoped_refptr<base::TaskRunner> io_task_runner);

  // Request that the Channel start processing IO events.
  virtual void Start() = 0;

  // Request that the Channel shut itself down.
  virtual void ShutDown() = 0;

  // Queues an outgoing message on the Channel. This message will either
  // eventually be written, or will fail to write and trigger Delegate::OnError.
  virtual void Write(OutgoingMessagePtr message) = 0;

 protected:
  Channel(Delegate* delegate);
  virtual ~Channel();

  base::Lock& read_lock() { return read_lock_; }
  ReadBuffer* read_buffer() { return &read_buffer_; }

  // Called by the implementation when new data is available in the read buffer.
  void OnReadCompleteNoLock(size_t bytes_read);

  // Called by the implementation when something goes horribly wrong.
  void OnError();

  virtual ScopedPlatformHandleVectorPtr GetReadPlatformHandlesNoLock(
      size_t num_handles) = 0;

 private:
  friend class base::RefCountedThreadSafe<Channel>;

  Delegate* delegate_;

  // Guards |read_buffer_| and the implementation's read platform handles if
  // applicable.
  base::Lock read_lock_;
  ReadBuffer read_buffer_;

  DISALLOW_COPY_AND_ASSIGN(Channel);
};

}  // namespace edk
}  // namespace mojo

#endif  // PORTS_MOJO_SYSTEM_CHANNEL_H_
