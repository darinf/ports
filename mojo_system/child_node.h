// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PORTS_MOJO_SYSTEM_CHILD_NODE_DELEGATE_H_
#define PORTS_MOJO_SYSTEM_CHILD_NODE_DELEGATE_H_

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "ports/mojo_system/node.h"
#include "ports/mojo_system/node_channel.h"

namespace mojo {
namespace edk {

class ChildNode : public Node {
 public:
  explicit ChildNode(scoped_ptr<NodeChannel> parent_channel);
  ~ChildNode() override;

 private:
  scoped_ptr<NodeChannel> parent_channel_;

  DISALLOW_COPY_AND_ASSIGN(ChildNode);
};

}  // namespace edk
}  // namespace mojo

#endif  // PORTS_MOJO_SYSTEM_CHILD_NODE_DELEGATE_H_
