// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PORTS_MOJO_SYSTEM_PARENT_NODE_H_
#define PORTS_MOJO_SYSTEM_PARENT_NODE_H_

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "ports/mojo_system/node.h"
#include "ports/mojo_system/node_channel.h"

namespace mojo {
namespace edk {

class ParentNode : public Node {
 public:
  ParentNode();
  ~ParentNode() override;

  void AddChild(scoped_ptr<NodeChannel> channel);

 private:
  DISALLOW_COPY_AND_ASSIGN(ParentNode);
};

}  // namespace edk
}  // namespace mojo

#endif  // PORTS_MOJO_SYSTEM_PARENT_NODE_H_
