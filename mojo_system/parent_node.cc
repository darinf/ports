// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ports/mojo_system/parent_node.h"

#include "base/logging.h"

namespace mojo {
namespace edk {

ParentNode::ParentNode() {}

ParentNode::~ParentNode() {}

void ParentNode::AddChild(scoped_ptr<NodeChannel> channel) {
  NOTIMPLEMENTED();
}

}  // namespace edk
}  // namespace mojo
