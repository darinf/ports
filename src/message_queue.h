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

#include "../include/ports.h"

namespace ports {

const uint32_t kInitialSequenceNum = 1;

class MessageQueue {
 public:
  MessageQueue();
  ~MessageQueue();

  bool IsEmpty();

  // Messages are ordered, so while the message queue may not be empty, this
  // method will return false if the *next* message is not available yet.
  bool HasNextMessage() const;
  
  // Gives ownership of the message.
  void GetNextMessage(Message** message);

  // Takes ownership of the message.
  void AcceptMessage(Message* message);

  // Used to block/unblock the Has/GetNextMessage methods. OK to call
  // Block/UnblockMessages multiple times, and UnblockMessages can be called in
  // advance of BlockMessages.
  void BlockMessages(int count);
  void UnblockMessages(int count);

 private:
  int block_count_;
  bool waiting_for_initial_message_;
};

}  // namespace ports

#endif  // PORTS_SRC_MESSAGE_QUEUE_H_
