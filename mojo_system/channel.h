// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PORTS_MOJO_SYSTEM_CHANNEL_H_
#define PORTS_MOJO_SYSTEM_CHANNEL_H_

#include <vector>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/task_runner.h"
#include "mojo/edk/embedder/platform_handle_vector.h"
#include "mojo/edk/embedder/scoped_platform_handle.h"

namespace mojo {
namespace edk {

// Channel provides a thread-safe interface to read and write byte streams and
// platform handles on some underlying IO channel.
class Channel : public base::RefCountedThreadSafe<Channel> {
 public:
  // A message read from the platform handle.
  struct IncomingMessage {
    // Doesn't own |data|. Does own |handles| and can transfer ownership.
    IncomingMessage(const void* data,
                    size_t num_bytes,
                    ScopedPlatformHandleVectorPtr handles);
    ~IncomingMessage();

    const void* data() const { return data_; }
    size_t num_bytes() const { return num_bytes_; }

    ScopedPlatformHandleVectorPtr TakeHandles() { return std::move(handles_); }

   private:
    const void* data_;
    const size_t num_bytes_;
    ScopedPlatformHandleVectorPtr handles_;

    DISALLOW_COPY_AND_ASSIGN(IncomingMessage);
  };

  // A message to be written to the platform handle.
  struct OutgoingMessage {
    // Takes ownership of |handles|.
    OutgoingMessage(std::vector<char> data,
                    ScopedPlatformHandleVectorPtr handles);
    ~OutgoingMessage();

    const void* data() const { return data_.data(); }
    size_t num_bytes() const { return data_.size(); }
    PlatformHandle* handles() {
      DCHECK(handles_);
      return static_cast<PlatformHandle*>(handles_->data());
    }
    size_t num_handles() const { return handles_ ? handles_->size() : 0; }

   private:
    std::vector<char> data_;
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

  // Creates a new Channel around a |platform_handle|, taking ownership of the
  // handle. All IO on the handle will be performed on |io_task_runner|.
  static scoped_refptr<Channel> Create(
      Delegate* delegate,
      ScopedPlatformHandle platform_handle,
      scoped_refptr<base::TaskRunner> io_task_runner);

  // Queues an outgoing message on the Channel. This message will either
  // eventually be written, or will fail to write and trigger Delegate::OnError.
  virtual void Write(OutgoingMessagePtr message) = 0;

 protected:
  Channel();
  virtual ~Channel() {}

 private:
  friend class base::RefCountedThreadSafe<Channel>;

  DISALLOW_COPY_AND_ASSIGN(Channel);
};

}  // namespace edk
}  // namespace mojo

#endif  // PORTS_MOJO_SYSTEM_CHANNEL_H_
