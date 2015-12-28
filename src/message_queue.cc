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

#include "ports/src/message_queue.h"

#include <algorithm>

namespace ports {

// Used by std::{push,pop}_heap functions
inline bool operator<(const ScopedMessage& a, const ScopedMessage& b) {
  return a->sequence_num > b->sequence_num;
}

MessageQueue::MessageQueue() : MessageQueue(kInitialSequenceNum) {}

MessageQueue::MessageQueue(uint32_t next_sequence_num)
    : next_sequence_num_(next_sequence_num) {
  // The message queue is blocked waiting for a message with sequence number
  // equal to |next_sequence_num|.
}

MessageQueue::~MessageQueue() {
}

void MessageQueue::GetNextMessageIf(MessageSelector* selector,
                                    ScopedMessage* message) {
  if (heap_.empty() || heap_[0]->sequence_num != next_sequence_num_) {
    message->reset();
  } else {
    if (selector && !selector->Select(*heap_[0].get())) {
      message->reset();
      return;
    }

    std::pop_heap(heap_.begin(), heap_.end());
    *message = std::move(heap_.back());
    heap_.pop_back();

    next_sequence_num_++;
  }
}

void MessageQueue::AcceptMessage(ScopedMessage message,
                                 bool* has_next_message) {
  // TODO: Handle sequence number roll-over.

  bool did_signal =
      !heap_.empty() && heap_[0]->sequence_num == next_sequence_num_;

  heap_.emplace_back(std::move(message));
  std::push_heap(heap_.begin(), heap_.end());

  // Only signal on transition to having the next message.
  if (did_signal || !signalable_) {
    *has_next_message = false;
  } else {
    *has_next_message = (heap_[0]->sequence_num == next_sequence_num_);
  }
}

}  // namespace ports
