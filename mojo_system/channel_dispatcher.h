// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PORTS_MOJO_SYSTEM_CHANNEL_DISPATCHER_H_
#define PORTS_MOJO_SYSTEM_CHANNEL_DISPATCHER_H_

#include <queue>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/synchronization/lock.h"
#include "base/task_runner.h"
#include "mojo/edk/embedder/scoped_platform_handle.h"
#include "mojo/edk/system/awakable_list.h"
#include "ports/mojo_system/channel.h"
#include "ports/mojo_system/dispatcher.h"

namespace mojo {
namespace edk {

// A Dispatcher which implements message pipe-like behavior on top of a Channel.
class ChannelDispatcher : public Dispatcher, public Channel::Delegate {
 public:
  ChannelDispatcher(ScopedPlatformHandle platform_handle,
                    scoped_refptr<base::TaskRunner> io_task_runner);

  Type GetType() const override;

 private:
  struct Message {
    // Creates and owns a copy of |data|. Takes ownership of |handles|.
    Message(const void* data,
            size_t num_bytes,
            ScopedPlatformHandleVectorPtr handles);
    ~Message();

    void* data() { return data_.data(); }
    size_t num_bytes() const { return data_.size(); }

    ScopedPlatformHandleVectorPtr TakeHandles() { return std::move(handles_); }

   private:
    std::vector<char> data_;
    ScopedPlatformHandleVectorPtr handles_;

    DISALLOW_COPY_AND_ASSIGN(Message);
  };

  using MessagePtr = scoped_ptr<Message>;

  // Dispatcher:
  MojoResult WriteMessageImplNoLock(ports::ScopedMessage message,
                                    MojoWriteMessageFlags flags) override;
  MojoResult ReadMessageImplNoLock(MojoReadMessageFlags flags,
                                   ports::ScopedMessage* message) override;
  HandleSignalsState GetHandleSignalsStateImplNoLock() const override;
  MojoResult AddAwakableImplNoLock(Awakable* awakable,
                                   MojoHandleSignals signals,
                                   uintptr_t context,
                                   HandleSignalsState* signals_state) override;
  void RemoveAwakableImplNoLock(Awakable* awakable,
                                HandleSignalsState* signals_state) override;

  // Channel::Delegate:
  void OnChannelRead(Channel::IncomingMessage* message) override;
  void OnChannelError() override;

  ~ChannelDispatcher() override;

  scoped_refptr<Channel> channel_;
  AwakableList awakables_;
  std::queue<MessagePtr> incoming_messages_;

  DISALLOW_COPY_AND_ASSIGN(ChannelDispatcher);
};

}  // namespace edk
}  // namespace mojo

#endif  // PORTS_MOJO_SYSTEM_CHANNEL_DISPATCHER_H_
