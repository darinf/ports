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

#ifndef PORTS_SRC_MESSAGE_QUEUE_H_
#define PORTS_SRC_MESSAGE_QUEUE_H_

#include <functional>
#include <memory>
#include <queue>
#include <vector>

#include "ports/include/ports.h"

namespace ports {

// Impacts priority_queue sorting.
inline bool operator<(const ScopedMessage& a, const ScopedMessage& b) {
  return a->sequence_num > b->sequence_num;
}

const uint32_t kInitialSequenceNum = 1;

class MessageQueue {
 public:
  MessageQueue();
  ~MessageQueue();

  uint32_t next_sequence_num() const { return next_sequence_num_; }
  bool may_signal() const { return may_signal_; }

  // Gives ownership of the message.
  void GetNextMessage(ScopedMessage* message);

  // Takes ownership of the message. Note: Messages are ordered, so while we
  // have added a message to the queue, we may still be waiting on a message
  // ahead of this one before we can let any of the messages be returned by
  // GetNextMessage.
  //
  // Furthermore, once has_next_message is set to true, it will remain false
  // until GetNextMessage is called enough times to return a null message.
  // In other words, has_next_message acts like an edge trigger.
  //
  void AcceptMessage(ScopedMessage message, bool* has_next_message);

 private:
  std::priority_queue<ScopedMessage> impl_;
  uint32_t next_sequence_num_;
  bool may_signal_;
};

}  // namespace ports

#endif  // PORTS_SRC_MESSAGE_QUEUE_H_
