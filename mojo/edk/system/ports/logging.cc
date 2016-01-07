// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "mojo/edk/system/ports/ports.h"

namespace ports {

std::ostream& operator<<(std::ostream& stream, const PortName& name) {
  std::ios::fmtflags flags(stream.flags());
  stream << std::hex << std::uppercase << name.value_major;
  if (name.value_minor != 0)
    stream << '.' << name.value_minor;
  stream.flags(flags);
  return stream;
}

std::ostream& operator<<(std::ostream& stream, const NodeName& name) {
  std::ios::fmtflags flags(stream.flags());
  stream << std::hex << std::uppercase << name.value_major;
  if (name.value_minor != 0)
    stream << '.' << name.value_minor;
  stream.flags(flags);
  return stream;
}

}  // namespace ports
