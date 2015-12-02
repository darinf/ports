// Copyright 2015, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "message_queue.h"

#include <assert.h>

namespace ports {

MessageQueue::MessageQueue()
    : next_sequence_num_(kInitialSequenceNum) {
  // The message queue is blocked waiting for a message with sequence number
  // equal to kInitialSequenceNum.
}

MessageQueue::~MessageQueue() {
}

bool MessageQueue::IsEmpty() {
  return impl_.empty();
}

void MessageQueue::GetNextMessage(ScopedMessage* message) {
  if (impl_.empty() || impl_.top()->sequence_num != next_sequence_num_) {
    message->reset();
  } else {
    // TODO: Surely there is a better way?
    const ScopedMessage* message_ptr = &impl_.top();
    message->reset(const_cast<ScopedMessage*>(message_ptr)->release());

    impl_.pop();
    next_sequence_num_++;
  }
}

void MessageQueue::AcceptMessage(ScopedMessage message,
                                 bool* has_next_message) {
  impl_.emplace(std::move(message));
  *has_next_message = (impl_.top()->sequence_num == next_sequence_num_);
}

void MessageQueue::Drain(std::deque<ScopedMessage>* storage) {
  impl_.Drain(storage);
}

void MessageQueue::Impl::Drain(std::deque<ScopedMessage>* storage) {
  assert(storage->empty());
  c.swap(*storage);
}

}  // namespace ports
