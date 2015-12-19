// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PORTS_MOJO_SYSTEM_PARENT_NODE_DELEGATE_H_
#define PORTS_MOJO_SYSTEM_PARENT_NODE_DELEGATE_H_

#include <unordered_map>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/task_runner.h"
#include "mojo/edk/embedder/scoped_platform_handle.h"
#include "ports/include/ports.h"
#include "ports/mojo_system/channel.h"
#include "ports/src/hash_functions.h"

namespace mojo {
namespace edk {

class ParentNodeDelegate : public ports::NodeDelegate {
 public:
  explicit ParentNodeDelegate(scoped_refptr<base::TaskRunner> io_task_runner);
  ~ParentNodeDelegate() override;

  void AddChild(ScopedPlatformHandle channel_to_child);

  // ports::NodeDelegate:
  void GenerateRandomPortName(ports::PortName* port_name) override;
  void SendEvent(const ports::NodeName& node, ports::Event e) override;
  void MessagesAvailable(const ports::PortName& port) override;

 private:
  ports::NodeName name_;
  scoped_ptr<ports::Node> node_;

  std::unordered_map<ports::NodeName, Channel*> nodes_;

  scoped_refptr<base::TaskRunner> io_task_runner_;

  DISALLOW_COPY_AND_ASSIGN(ParentNodeDelegate);
};

}  // namespace edk
}  // namespace mojo

#endif  // PORTS_MOJO_SYSTEM_PARENT_NODE_DELEGATE_H_
