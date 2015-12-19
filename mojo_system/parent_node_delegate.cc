// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/logging.h"
#include "base/stl_util.h"
#include "crypto/random.h"
#include "ports/mojo_system/parent_node_delegate.h"

namespace mojo {
namespace edk {

namespace {

template <typename T>
void Randomize(T* out) { crypto::RandBytes(out, sizeof(T)); }

}  // namespace

ParentNodeDelegate::ParentNodeDelegate(
    scoped_refptr<base::TaskRunner> io_task_runner)
    : io_task_runner_(io_task_runner) {
  Randomize(&name_);
  node_.reset(new ports::Node(name_, this));

  DLOG(INFO) << "Initialized parent node named " << name_;
}

ParentNodeDelegate::~ParentNodeDelegate() {
  STLDeleteContainerPairSecondPointers(nodes_.begin(), nodes_.end());
}

void ParentNodeDelegate::AddChild(ScopedPlatformHandle channel_to_child) {
  ports::NodeName child_name;
  Randomize(&child_name);
  DLOG(INFO) << "Assigning name " << child_name << " to new child";

  scoped_ptr<Channel> channel = Channel::Create(std::move(channel_to_child));

  // TODO: Don't write directly to the pipe here.
  channel->Write(static_cast<const void*>(&child_name),
                 sizeof(ports::NodeName));
  channel->Write(static_cast<const void*>(&name_), sizeof(ports::NodeName));
  nodes_.insert(std::make_pair(child_name, channel.release()));
}

void ParentNodeDelegate::GenerateRandomPortName(ports::PortName* port_name) {
  Randomize(port_name);
}

void ParentNodeDelegate::SendEvent(const ports::NodeName& node,
                                   ports::Event e) {
  NOTIMPLEMENTED();
}

void ParentNodeDelegate::MessagesAvailable(const ports::PortName& port) {
  NOTIMPLEMENTED();
}

}  // namespace edk
}  // namespace mojo
