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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <queue>

#include "../include/ports.h"

#include <gtest/gtest.h>

namespace ports {
namespace test {

static void PrintMessage(const Message* message) {
  printf(":[seq=%u]\"%s\"[",
      message->sequence_num, static_cast<const char*>(message->bytes));
  for (size_t i = 0; i < message->num_ports; ++i) {
    if (i > 0)
      printf(",");
    printf("p%lX", message->ports[i].name.value);
  }
  printf("]");
}

struct Task {
  Task(NodeName node_name, Event event)
      : node_name(node_name),
        event(std::move(event)),
        priority(rand()) {
  }

  NodeName node_name;
  Event event;
  int32_t priority;
};

struct TaskComparator {
  bool operator()(const Task* a, const Task* b) {
    return a->priority < b->priority;
  }
};

static std::priority_queue<Task*,
                           std::vector<Task*>,
                           TaskComparator> task_queue;
static Node* node_map[2];

static void PumpTasks() {
  while (!task_queue.empty()) {
    Task* task = task_queue.top();
    task_queue.pop();

    Node* node = node_map[task->node_name.value];
    node->AcceptEvent(std::move(task->event));

    delete task;
  }
}

static ScopedMessage NewStringMessage(const char* s) {
  size_t len = strlen(s) + 1;
  Message* message = AllocMessage(len, 0); 
  memcpy(message->bytes, s, len);
  return ScopedMessage(message);
}

static ScopedMessage NewStringMessageWithPort(const char* s, PortName port) {
  size_t len = strlen(s) + 1;
  Message* message = AllocMessage(len, 1); 
  memcpy(message->bytes, s, len);
  message->ports[0].name = port;
  return ScopedMessage(message);
}

class TestNodeDelegate : public NodeDelegate {
 public:
  explicit TestNodeDelegate(NodeName node_name)
      : node_name_(node_name),
        drop_events_(false) {
  }

  void DropEvents() { drop_events_ = true; }

  virtual PortName GenerateRandomPortName() override {
    static uint64_t next_port_name = 1;
    return PortName(next_port_name++);
  }

  virtual void SendEvent(NodeName node_name, Event event) override {
    if (drop_events_) {
      printf("n%lX:dropping event %d to %lX@%lX\n",
          node_name_.value,
          event.type,
          node_name.value,
          event.port_name.value);
      return;
    }
    task_queue.push(new Task(node_name, std::move(event)));
  }

  virtual void MessagesAvailable(PortName port) override {
    Node* node = node_map[node_name_.value];
    for (;;) {
      ScopedMessage message;
      if (node->GetMessage(port, &message) != OK || !message)
        break;
      printf("n%lX:MessagesAvailable(p%lX)", node_name_.value, port.value);
      PrintMessage(message.get());
      printf("\n");
      for (size_t i = 0; i < message->num_ports; ++i) {
        char buf[256];
        snprintf(buf, sizeof(buf), "got port: p%lX",
                 message->ports[i].name.value);
        node->SendMessage(message->ports[i].name,
                          std::move(NewStringMessage(buf)));
      }
    }
  }

 private:
  NodeName node_name_;
  bool drop_events_;
};

class PortsTest : public testing::Test {
 public:
  PortsTest() {
    node_map[0] = nullptr;
    node_map[1] = nullptr;
  }
};

TEST_F(PortsTest, Basic) {
  NodeName node0_name(0);
  TestNodeDelegate node0_delegate(node0_name);
  Node node0(node0_name, &node0_delegate);
  node_map[0] = &node0;

  NodeName node1_name(1);
  TestNodeDelegate node1_delegate(node1_name);
  Node node1(node1_name, &node1_delegate);
  node_map[1] = &node1;

  // Setup pipe between node0 and node1.
  PortName x0, x1;
  node0.CreatePort(&x0);
  node1.CreatePort(&x1);
  node0.InitializePort(x0, node1_name, x1);
  node1.InitializePort(x1, node0_name, x0);

  // Transfer a message from node0 to node1.
  node0.SendMessage(x0, NewStringMessage("hello world"));

  // Transfer a port from node0 to node1.
  PortName a0, a1;
  node0.CreatePortPair(&a0, &a1);
  node0.SendMessage(x0, NewStringMessageWithPort("take port", a1));
  node0.SendMessage(a0, NewStringMessage("hello over there"));

  // Transfer a0 as well.
  node0.SendMessage(x0, NewStringMessageWithPort("take another port", a0));

  PortName b0, b1;
  node0.CreatePortPair(&b0, &b1);
  node0.SendMessage(x0, NewStringMessageWithPort("take port (2)", b1));
  node0.SendMessage(b0, NewStringMessage("hello over there (2)"));

  // This may cause a SendMessage(b1) failure.
  node0.ClosePort(b0);

  PumpTasks();

  if (node0.Shutdown() == OK_SHUTDOWN_DELAYED)
    printf("n0:shutdown delayed\n");

  PumpTasks();

  if (node1.Shutdown() == OK_SHUTDOWN_DELAYED)
    printf("n1:shutdown delayed\n");

  PumpTasks();

  EXPECT_EQ(OK, node0.Shutdown());
  EXPECT_EQ(OK, node1.Shutdown());
}

TEST_F(PortsTest, LostConnectionToNode) {
  NodeName node0_name(0);
  TestNodeDelegate node0_delegate(node0_name);
  Node node0(node0_name, &node0_delegate);
  node_map[0] = &node0;

  NodeName node1_name(1);
  TestNodeDelegate node1_delegate(node1_name);
  Node node1(node1_name, &node1_delegate);
  node_map[1] = &node1;

  // Setup pipe between node0 and node1.
  PortName x0, x1;
  node0.CreatePort(&x0);
  node1.CreatePort(&x1);
  node0.InitializePort(x0, node1_name, x1);
  node1.InitializePort(x1, node0_name, x0);

  // Transfer port to node1 and simulate a lost connection to node1. Dropping
  // events from node1 is how we simulate the lost connection.

  node1_delegate.DropEvents();

  PortName a0, a1;
  node0.CreatePortPair(&a0, &a1);
  node0.SendMessage(x0, NewStringMessageWithPort("take port", a1));

  PumpTasks();

  node0.LostConnectionToNode(node1_name);

  PumpTasks();

  EXPECT_EQ(OK, node0.Shutdown());
  EXPECT_EQ(OK, node1.Shutdown());
}

}  // namespace test
}  // namespace ports
