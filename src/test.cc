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
#include <sstream>

#include "ports/include/ports.h"
#include "ports/src/logging.h"

#include <gtest/gtest.h>

namespace ports {
namespace test {

static void LogMessage(const Message* message) {
  std::stringstream ports;
  for (size_t i = 0; i < message->num_ports; ++i) {
    if (i > 0)
      ports << ",";
    ports << message->ports[i].name;
  }
  DLOG(INFO) << "message: seq_num=" << message->sequence_num
             << " payload=\"" << static_cast<const char*>(message->bytes)
             << "\" ports=[" << ports.str() << "]";
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

static Node* GetNode(const NodeName& name) {
  return node_map[name.value_major];
}
static void SetNode(const NodeName& name, Node* node) {
  node_map[name.value_major] = node;
}

static void PumpTasks() {
  while (!task_queue.empty()) {
    Task* task = task_queue.top();
    task_queue.pop();

    Node* node = GetNode(task->node_name);
    node->AcceptEvent(std::move(task->event));

    delete task;
  }
}

static void DiscardPendingTasks() {
  while (!task_queue.empty()) {
    Task* task = task_queue.top();
    task_queue.pop();
    delete task;
  }
}

static ScopedMessage NewStringMessage(const std::string& s) {
  size_t size = s.size() + 1;
  Message* message = AllocMessage(size, 0);
  memcpy(message->bytes, s.data(), size);
  return ScopedMessage(message);
}

static ScopedMessage NewStringMessageWithPort(const std::string& s,
                                              PortName port) {
  size_t size = s.size() + 1;
  Message* message = AllocMessage(size, 1);
  memcpy(message->bytes, s.data(), size);
  message->ports[0].name = port;
  return ScopedMessage(message);
}

class TestNodeDelegate : public NodeDelegate {
 public:
  explicit TestNodeDelegate(const NodeName& node_name)
      : node_name_(node_name),
        drop_events_(false),
        read_messages_(true),
        save_messages_(false) {
  }

  void set_drop_events(bool value) { drop_events_ = value; }
  void set_read_messages(bool value) { read_messages_ = value; }
  void set_save_messages(bool value) { save_messages_ = value; }

  bool GetSavedMessage(ScopedMessage* message) {
    if (saved_messages_.empty()) {
      message->reset();
      return false;
    }
    *message = std::move(saved_messages_.front());
    saved_messages_.pop();
    return true;
  }

  void GenerateRandomPortName(PortName* port_name) override {
    static uint64_t next_port_name = 1;
    port_name->value_major = next_port_name++;
    port_name->value_minor = 0;
  }

  void SendEvent(const NodeName& node_name, Event event) override {
    if (drop_events_) {
      DLOG(INFO) << "Dropping SendEvent(" << event.type << ") from node "
                 << node_name_ << " to "
                 << event.port_name << "@" << node_name;
      return;
    }
    DLOG(INFO) << "SendEvent(" << event.type << ") from node "
               << node_name_ << " to "
               << event.port_name << "@" << node_name;
    task_queue.push(new Task(node_name, std::move(event)));
  }

  void MessagesAvailable(const PortName& port) override {
    DLOG(INFO) << "MessagesAvailable for " << port << "@" << node_name_;
    if (!read_messages_)
      return;
    Node* node = GetNode(node_name_);
    for (;;) {
      ScopedMessage message;
      int rv = node->GetMessage(port, &message);
      EXPECT_TRUE(rv == OK || rv == ERROR_PORT_PEER_CLOSED);
      if (rv == ERROR_PORT_PEER_CLOSED || !message)
        break;
      if (save_messages_) {
        SaveMessage(std::move(message));
      } else {
        LogMessage(message.get());
        for (size_t i = 0; i < message->num_ports; ++i) {
          std::stringstream buf;
          buf << "got port: " << message->ports[i].name;
          node->SendMessage(message->ports[i].name,
                            NewStringMessage(buf.str()));

          // Avoid leaking these ports.
          node->ClosePort(message->ports[i].name);
        }
      }
    }
  }

 private:
  void SaveMessage(ScopedMessage message) {
    saved_messages_.emplace(std::move(message));
  }

  std::queue<ScopedMessage> saved_messages_;
  NodeName node_name_;
  bool drop_events_;
  bool read_messages_;
  bool save_messages_;
};

class PortsTest : public testing::Test {
 public:
  PortsTest() {
    SetNode(NodeName(0, 0), nullptr);
    SetNode(NodeName(1, 0), nullptr);
  }

  ~PortsTest() override {
    DiscardPendingTasks();
    SetNode(NodeName(0, 0), nullptr);
    SetNode(NodeName(1, 0), nullptr);
  }
};

TEST_F(PortsTest, Basic1) {
  NodeName node0_name(0, 0);
  TestNodeDelegate node0_delegate(node0_name);
  Node node0(node0_name, &node0_delegate);
  SetNode(node0_name, &node0);

  NodeName node1_name(1, 0);
  TestNodeDelegate node1_delegate(node1_name);
  Node node1(node1_name, &node1_delegate);
  SetNode(node1_name, &node1);

  // Setup pipe between node0 and node1.
  PortName x0, x1;
  EXPECT_EQ(OK, node0.CreatePort(&x0));
  EXPECT_EQ(OK, node1.CreatePort(&x1));
  EXPECT_EQ(OK, node0.InitializePort(x0, node1_name, x1));
  EXPECT_EQ(OK, node1.InitializePort(x1, node0_name, x0));

  // Transfer a port from node0 to node1.
  PortName a0, a1;
  EXPECT_EQ(OK, node0.CreatePortPair(&a0, &a1));
  EXPECT_EQ(OK, node0.SendMessage(x0, NewStringMessageWithPort("hello", a1)));

  EXPECT_EQ(OK, node0.ClosePort(a0));

  EXPECT_EQ(OK, node0.ClosePort(x0));
  EXPECT_EQ(OK, node1.ClosePort(x1));
}

TEST_F(PortsTest, Basic2) {
  NodeName node0_name(0, 0);
  TestNodeDelegate node0_delegate(node0_name);
  Node node0(node0_name, &node0_delegate);
  SetNode(node0_name, &node0);

  NodeName node1_name(1, 0);
  TestNodeDelegate node1_delegate(node1_name);
  Node node1(node1_name, &node1_delegate);
  SetNode(node1_name, &node1);

  // Setup pipe between node0 and node1.
  PortName x0, x1;
  EXPECT_EQ(OK, node0.CreatePort(&x0));
  EXPECT_EQ(OK, node1.CreatePort(&x1));
  EXPECT_EQ(OK, node0.InitializePort(x0, node1_name, x1));
  EXPECT_EQ(OK, node1.InitializePort(x1, node0_name, x0));

  PortName b0, b1;
  EXPECT_EQ(OK, node0.CreatePortPair(&b0, &b1));
  EXPECT_EQ(OK, node0.SendMessage(x0, NewStringMessageWithPort("hello", b1)));
  EXPECT_EQ(OK, node0.SendMessage(b0, NewStringMessage("hello again")));

  // This may cause a SendMessage(b1) failure.
  EXPECT_EQ(OK, node0.ClosePort(b0));

  EXPECT_EQ(OK, node0.ClosePort(x0));
  EXPECT_EQ(OK, node1.ClosePort(x1));
}

TEST_F(PortsTest, Basic3) {
  NodeName node0_name(0, 0);
  TestNodeDelegate node0_delegate(node0_name);
  Node node0(node0_name, &node0_delegate);
  SetNode(node0_name, &node0);

  NodeName node1_name(1, 0);
  TestNodeDelegate node1_delegate(node1_name);
  Node node1(node1_name, &node1_delegate);
  SetNode(node1_name, &node1);

  // Setup pipe between node0 and node1.
  PortName x0, x1;
  EXPECT_EQ(OK, node0.CreatePort(&x0));
  EXPECT_EQ(OK, node1.CreatePort(&x1));
  EXPECT_EQ(OK, node0.InitializePort(x0, node1_name, x1));
  EXPECT_EQ(OK, node1.InitializePort(x1, node0_name, x0));

  // Transfer a port from node0 to node1.
  PortName a0, a1;
  EXPECT_EQ(OK, node0.CreatePortPair(&a0, &a1));
  EXPECT_EQ(OK, node0.SendMessage(x0, NewStringMessageWithPort("hello", a1)));
  EXPECT_EQ(OK, node0.SendMessage(a0, NewStringMessage("hello again")));

  // Transfer a0 as well.
  EXPECT_EQ(OK, node0.SendMessage(x0, NewStringMessageWithPort("foo", a0)));

  PortName b0, b1;
  EXPECT_EQ(OK, node0.CreatePortPair(&b0, &b1));
  EXPECT_EQ(OK, node0.SendMessage(x0, NewStringMessageWithPort("bar", b1)));
  EXPECT_EQ(OK, node0.SendMessage(b0, NewStringMessage("baz")));

  // This may cause a SendMessage(b1) failure.
  EXPECT_EQ(OK, node0.ClosePort(b0));

  EXPECT_EQ(OK, node0.ClosePort(x0));
  EXPECT_EQ(OK, node1.ClosePort(x1));
}

TEST_F(PortsTest, LostConnectionToNode1) {
  NodeName node0_name(0, 0);
  TestNodeDelegate node0_delegate(node0_name);
  Node node0(node0_name, &node0_delegate);
  SetNode(node0_name, &node0);

  NodeName node1_name(1, 0);
  TestNodeDelegate node1_delegate(node1_name);
  Node node1(node1_name, &node1_delegate);
  SetNode(node1_name, &node1);

  // Setup pipe between node0 and node1.
  PortName x0, x1;
  EXPECT_EQ(OK, node0.CreatePort(&x0));
  EXPECT_EQ(OK, node1.CreatePort(&x1));
  EXPECT_EQ(OK, node0.InitializePort(x0, node1_name, x1));
  EXPECT_EQ(OK, node1.InitializePort(x1, node0_name, x0));

  // Transfer port to node1 and simulate a lost connection to node1. Dropping
  // events from node1 is how we simulate the lost connection.

  node1_delegate.set_drop_events(true);

  PortName a0, a1;
  EXPECT_EQ(OK, node0.CreatePortPair(&a0, &a1));
  EXPECT_EQ(OK, node0.SendMessage(x0, NewStringMessageWithPort("foo", a1)));

  PumpTasks();

  EXPECT_EQ(OK, node0.LostConnectionToNode(node1_name));

  PumpTasks();

  EXPECT_EQ(OK, node0.ClosePort(a0));
  EXPECT_EQ(OK, node0.ClosePort(x0));
  EXPECT_EQ(OK, node1.ClosePort(x1));
}

TEST_F(PortsTest, LostConnectionToNode2) {
  NodeName node0_name(0, 0);
  TestNodeDelegate node0_delegate(node0_name);
  Node node0(node0_name, &node0_delegate);
  node_map[0] = &node0;

  NodeName node1_name(1, 0);
  TestNodeDelegate node1_delegate(node1_name);
  Node node1(node1_name, &node1_delegate);
  node_map[1] = &node1;

  // Setup pipe between node0 and node1.
  PortName x0, x1;
  EXPECT_EQ(OK, node0.CreatePort(&x0));
  EXPECT_EQ(OK, node1.CreatePort(&x1));
  EXPECT_EQ(OK, node0.InitializePort(x0, node1_name, x1));
  EXPECT_EQ(OK, node1.InitializePort(x1, node0_name, x0));

  node1_delegate.set_read_messages(false);

  PortName a0, a1;
  EXPECT_EQ(OK, node0.CreatePortPair(&a0, &a1));
  EXPECT_EQ(OK, node0.SendMessage(x0, NewStringMessageWithPort("take a1", a1)));

  PumpTasks();

  node1_delegate.set_drop_events(true);

  EXPECT_EQ(OK, node0.LostConnectionToNode(node1_name));

  PumpTasks();

  ScopedMessage message;
  EXPECT_EQ(ERROR_PORT_PEER_CLOSED, node0.GetMessage(a0, &message));
  EXPECT_FALSE(message);
}

TEST_F(PortsTest, GetMessage1) {
  NodeName node0_name(0, 0);
  TestNodeDelegate node0_delegate(node0_name);
  Node node0(node0_name, &node0_delegate);
  node_map[0] = &node0;

  PortName a0, a1;
  EXPECT_EQ(OK, node0.CreatePortPair(&a0, &a1));

  ScopedMessage message;
  EXPECT_EQ(OK, node0.GetMessage(a0, &message));
  EXPECT_FALSE(message);

  EXPECT_EQ(OK, node0.ClosePort(a1));

  EXPECT_EQ(OK, node0.GetMessage(a0, &message));
  EXPECT_FALSE(message);

  PumpTasks();

  EXPECT_EQ(ERROR_PORT_PEER_CLOSED, node0.GetMessage(a0, &message));
  EXPECT_FALSE(message);

  EXPECT_EQ(OK, node0.ClosePort(a0));
}

TEST_F(PortsTest, GetMessage2) {
  NodeName node0_name(0, 0);
  TestNodeDelegate node0_delegate(node0_name);
  Node node0(node0_name, &node0_delegate);
  node_map[0] = &node0;

  node0_delegate.set_read_messages(false);

  PortName a0, a1;
  EXPECT_EQ(OK, node0.CreatePortPair(&a0, &a1));

  EXPECT_EQ(OK, node0.SendMessage(a1, NewStringMessage("1")));

  ScopedMessage message;
  EXPECT_EQ(OK, node0.GetMessage(a0, &message));
  EXPECT_FALSE(message);

  PumpTasks();

  EXPECT_EQ(OK, node0.GetMessage(a0, &message));
  ASSERT_TRUE(message);
  EXPECT_EQ(0, strcmp(static_cast<char*>(message->bytes), "1"));

  EXPECT_EQ(OK, node0.ClosePort(a0));
  EXPECT_EQ(OK, node0.ClosePort(a1));
}

TEST_F(PortsTest, GetMessage3) {
  NodeName node0_name(0, 0);
  TestNodeDelegate node0_delegate(node0_name);
  Node node0(node0_name, &node0_delegate);
  node_map[0] = &node0;

  node0_delegate.set_read_messages(false);

  PortName a0, a1;
  EXPECT_EQ(OK, node0.CreatePortPair(&a0, &a1));

  const char* kStrings[] = {
    "1",
    "2",
    "3"
  };

  for (size_t i = 0; i < sizeof(kStrings)/sizeof(kStrings[0]); ++i)
    EXPECT_EQ(OK, node0.SendMessage(a1, NewStringMessage(kStrings[i])));

  ScopedMessage message;
  EXPECT_EQ(OK, node0.GetMessage(a0, &message));
  EXPECT_FALSE(message);

  PumpTasks();

  for (size_t i = 0; i < sizeof(kStrings)/sizeof(kStrings[0]); ++i) {
    EXPECT_EQ(OK, node0.GetMessage(a0, &message));
    ASSERT_TRUE(message);
    EXPECT_EQ(0, strcmp(static_cast<char*>(message->bytes), kStrings[i]));
    DLOG(INFO) << "got " << kStrings[i];
  }

  EXPECT_EQ(OK, node0.ClosePort(a0));
  EXPECT_EQ(OK, node0.ClosePort(a1));
}

TEST_F(PortsTest, Delegation1) {
  NodeName node0_name(0, 0);
  TestNodeDelegate node0_delegate(node0_name);
  Node node0(node0_name, &node0_delegate);
  SetNode(node0_name, &node0);

  NodeName node1_name(1, 0);
  TestNodeDelegate node1_delegate(node1_name);
  Node node1(node1_name, &node1_delegate);
  node_map[1] = &node1;

  node0_delegate.set_save_messages(true);
  node1_delegate.set_save_messages(true);

  // Setup pipe between node0 and node1.
  PortName x0, x1;
  EXPECT_EQ(OK, node0.CreatePort(&x0));
  EXPECT_EQ(OK, node1.CreatePort(&x1));
  EXPECT_EQ(OK, node0.InitializePort(x0, node1_name, x1));
  EXPECT_EQ(OK, node1.InitializePort(x1, node0_name, x0));

  // In this test, we send a message to a port that has been moved.

  PortName a0, a1;
  EXPECT_EQ(OK, node0.CreatePortPair(&a0, &a1));

  EXPECT_EQ(OK, node0.SendMessage(x0, NewStringMessageWithPort("a1", a1)));

  PumpTasks();

  ScopedMessage message;
  ASSERT_TRUE(node1_delegate.GetSavedMessage(&message));

  ASSERT_EQ(1u, message->num_ports);

  // This is "a1" from the point of view of node1.
  PortName a2 = message->ports[0].name;

  EXPECT_EQ(OK, node1.SendMessage(x1, NewStringMessageWithPort("a2", a2)));

  PumpTasks();

  EXPECT_EQ(OK, node0.SendMessage(a0, NewStringMessage("hello")));

  PumpTasks();

  ASSERT_TRUE(node0_delegate.GetSavedMessage(&message));

  ASSERT_EQ(1u, message->num_ports);

  // This is "a2" from the point of view of node1.
  PortName a3 = message->ports[0].name;

  EXPECT_EQ(0, strcmp("a2", static_cast<char*>(message->bytes)));

  ASSERT_TRUE(node0_delegate.GetSavedMessage(&message));

  EXPECT_EQ(0u, message->num_ports);
  EXPECT_EQ(0, strcmp("hello", static_cast<char*>(message->bytes)));

  EXPECT_EQ(OK, node0.ClosePort(a0));
  EXPECT_EQ(OK, node0.ClosePort(a3));

  EXPECT_EQ(OK, node0.ClosePort(x0));
  EXPECT_EQ(OK, node1.ClosePort(x1));
}

}  // namespace test
}  // namespace ports
