// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ports/mojo_system/child_node.h"

namespace mojo {
namespace edk {

ChildNode::ChildNode(scoped_ptr<NodeChannel> parent_channel)
    : parent_channel_(std::move(parent_channel)) {}

ChildNode::~ChildNode() {}

}  // namespace edk
}  // namespace mojo
