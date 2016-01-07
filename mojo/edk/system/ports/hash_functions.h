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

#ifndef PORTS_SRC_HASH_FUNCTIONS_H_
#define PORTS_SRC_HASH_FUNCTIONS_H_

#include "mojo/edk/system/ports/ports.h"

#include <functional>

namespace std {

template <>
struct hash<ports::PortName> {
  std::size_t operator()(const ports::PortName& port_name) const {
    size_t h1 = hash<uint64_t>()(port_name.value_major);
    size_t h2 = hash<uint64_t>()(port_name.value_minor);
    return h1 ^ (h2 << 1);
  }
};

template <>
struct hash<ports::NodeName> {
  std::size_t operator()(const ports::NodeName& node_name) const {
    size_t h1 = hash<uint64_t>()(node_name.value_major);
    size_t h2 = hash<uint64_t>()(node_name.value_minor);
    return h1 ^ (h2 << 1);
  }
};

}  // namespace std

#endif  // PORTS_SRC_HASH_FUNCTIONS_H_
