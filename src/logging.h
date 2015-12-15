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

#ifndef PORTS_SRC_LOGGING_H_
#define PORTS_SRC_LOGGING_H_

#include <sstream>

#include "ports/include/ports.h"

namespace ports {

inline std::ostream& operator<<(std::ostream& stream, const PortName& name) {
  stream << std::hex << std::uppercase << name.value_major;
  if (name.value_minor != 0)
    stream << '.' << name.value_minor;
  return stream;
}

inline std::ostream& operator<<(std::ostream& stream, const NodeName& name) {
  stream << std::hex << std::uppercase << name.value_major;
  if (name.value_minor != 0)
    stream << '.' << name.value_minor;
  return stream;
}

class Logger {
 public:
  ~Logger();
  std::ostream& stream();
};

class Voidify {
 public:
  void operator&(std::ostream&) {}
};

}  // namespace ports

#define PORTS_LOG(severity) ::ports::Logger().stream()
#define PORTS_LOG_NONE(severity) \
    true ? (void) 0 : ::ports::Voidify() & ::ports::Logger().stream()

#define PORTS_LOGGING_ENABLED 1

#ifdef PORTS_LOGGING_ENABLED
#define LOG PORTS_LOG
#ifndef NDEBUG
#define DLOG PORTS_LOG
#else
#define DLOG PORTS_LOG_NONE
#endif
#else
#define LOG PORTS_LOG_NONE
#define DLOG PORTS_LOG_NONE
#endif

#endif  // PORTS_SRC_LOGGING_H_
