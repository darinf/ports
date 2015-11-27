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

#include "../include/ports.h"

#include <stdlib.h>

namespace ports {

Message* AllocMessage(size_t num_bytes, size_t num_dependent_ports) {
  // Memory layout: [Message] [dependent_ports] [bytes]

  size_t size =
      sizeof(Message) + num_dependent_ports * sizeof(PortName) + num_bytes;

  char* memory = static_cast<char*>(malloc(size));

  Message* message = reinterpret_cast<Message*>(memory);
  message->sequence_num = 0;
  message->dependent_ports =
      reinterpret_cast<PortName*>(memory + sizeof(Message));
  message->num_dependent_ports = num_dependent_ports;
  message->bytes =
      memory + sizeof(Message) + num_dependent_ports * sizeof(PortName);
  message->num_bytes = num_bytes;

  return message;
}

void FreeMessage(Message* message) {
  char* memory = reinterpret_cast<char*>(message);
  free(memory);
}

}  // namespace ports
