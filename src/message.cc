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

#include "ports/include/ports.h"

#include <stdlib.h>

#include <limits>

#include "event.h"
#include "logging.h"

namespace ports {

// static
void Message::Parse(const void* bytes,
                    size_t num_bytes,
                    size_t* num_header_bytes,
                    size_t* num_payload_bytes,
                    size_t* num_ports_bytes) {
  const EventHeader* header = static_cast<const EventHeader*>(bytes);
  switch (header->type) {
    case EventType::kUser:
      // See below.
      break;
    case EventType::kPortAccepted:
      *num_header_bytes = sizeof(EventHeader);
      break;
    case EventType::kObserveProxy:
      *num_header_bytes = sizeof(EventHeader) + sizeof(ObserveProxyEventData);
      break;
    case EventType::kObserveProxyAck:
      *num_header_bytes =
          sizeof(EventHeader) + sizeof(ObserveProxyAckEventData);
      break;
    case EventType::kObserveClosure:
      *num_header_bytes = sizeof(EventHeader) + sizeof(ObserveClosureEventData);
      break;
  }

  if (header->type == EventType::kUser) {
    const UserEventData* event_data =
        reinterpret_cast<const UserEventData*>(
            reinterpret_cast<const char*>(header + 1));
    *num_header_bytes = sizeof(EventHeader) +
                        sizeof(UserEventData) +
                        event_data->num_ports * sizeof(PortDescriptor);
    *num_ports_bytes = event_data->num_ports * sizeof(PortName);
    *num_payload_bytes = num_bytes - *num_header_bytes - *num_ports_bytes;
  } else {
    *num_payload_bytes = 0;
    *num_ports_bytes = 0;
    DCHECK_EQ(num_bytes, *num_header_bytes);
  }
}

Message::Message(size_t num_header_bytes,
                 size_t num_payload_bytes,
                 size_t num_ports_bytes)
    : start_(nullptr),
      num_header_bytes_(num_header_bytes),
      num_ports_bytes_(num_ports_bytes),
      num_payload_bytes_(num_payload_bytes) {
}

}  // namespace ports
