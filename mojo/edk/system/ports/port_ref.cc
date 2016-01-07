// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/edk/system/ports/ports.h"

#include "mojo/edk/system/ports/port.h"

namespace ports {

PortRef::~PortRef() {
}

PortRef::PortRef() {
}

PortRef::PortRef(const PortName& name, std::shared_ptr<Port> port)
    : name_(name), port_(std::move(port)) {
}

PortRef::PortRef(const PortRef& other)
    : name_(other.name_), port_(other.port_) {
}

PortRef& PortRef::operator=(const PortRef& other) {
  if (&other != this) {
    name_ = other.name_;
    port_ = other.port_;
  }
  return *this;
}

}  // namespace ports