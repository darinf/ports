// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <deque>

#include "base/logging.h"
#include "crypto/random.h"
#include "ports/mojo_system/child_node_delegate.h"

namespace mojo {
namespace edk {

ChildNodeDelegate::ChildNodeDelegate(
    ScopedPlatformHandle channel_to_parent,
    scoped_refptr<base::TaskRunner> io_task_runner)
    : channel_to_parent_(Channel::Create(std::move(channel_to_parent))),
      io_task_runner_(io_task_runner) {
  std::deque<PlatformHandle> handles;

  // TODO: Don't read directly from the pipe here.
  channel_to_parent_->Read(
      static_cast<void*>(&name_), sizeof(ports::NodeName), &handles);
  CHECK(handles.empty());

  channel_to_parent_->Read(
      static_cast<void*>(&parent_name_), sizeof(ports::NodeName), &handles);
  CHECK(handles.empty());

  DLOG(INFO) << "Initializing child named " << name_ << " with parent named "
      << parent_name_;
  node_.reset(new ports::Node(name_, this));
}

ChildNodeDelegate::~ChildNodeDelegate() {}

void ChildNodeDelegate::GenerateRandomPortName(ports::PortName* port_name) {
  crypto::RandBytes(port_name, sizeof(ports::PortName));
}

void ChildNodeDelegate::SendEvent(const ports::NodeName& node, ports::Event e) {
  NOTIMPLEMENTED();
}

void ChildNodeDelegate::MessagesAvailable(const ports::PortName& port) {
  NOTIMPLEMENTED();
}

}  // namespace edk
}  // namespace mojo
