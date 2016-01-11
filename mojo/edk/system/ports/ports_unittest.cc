// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <map>
#include <memory>
#include <queue>
#include <sstream>

#include "base/logging.h"
#include "mojo/edk/system/ports/node.h"
#include "mojo/edk/system/ports/node_delegate.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace mojo {
namespace edk {
namespace ports {
namespace test {

static void LogMessage(const Message* message) {
  std::stringstream ports;
  for (size_t i = 0; i < message->num_ports(); ++i) {
    if (i > 0)
      ports << ",";
    ports << message->ports()[i];
  }
  DVLOG(1) << "message: \""
           << static_cast<const char*>(message->payload_bytes())
           << "\" ports=[" << ports.str() << "]";
}

class TestMessage : public Message {
 public:
  TestMessage(size_t num_header_bytes,
              size_t num_payload_bytes,
              size_t num_ports_bytes)
      : Message(num_header_bytes,
                num_payload_bytes,
                num_ports_bytes) {
    start_ = new char[num_header_bytes + num_payload_bytes + num_ports_bytes];
  }

  ~TestMessage() override {
    delete[] start_;
  }
};

struct Task {
  Task(NodeName node_name, ScopedMessage message)
      : node_name(node_name),
        message(std::move(message)),
        priority(rand()) {
  }

  NodeName node_name;
  ScopedMessage message;
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
  return node_map[name.v1];
}
static void SetNode(const NodeName& name, Node* node) {
  node_map[name.v1] = node;
}

static void PumpTasks() {
  while (!task_queue.empty()) {
    Task* task = task_queue.top();
    task_queue.pop();

    Node* node = GetNode(task->node_name);
    node->AcceptMessage(std::move(task->message));

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
  ScopedMessage message;
  EXPECT_EQ(OK, node_map[0]->AllocMessage(size, 0, &message));
  memcpy(message->mutable_payload_bytes(), s.data(), size);
  return message;
}

static ScopedMessage NewStringMessageWithPort(const std::string& s,
                                              const PortName& port_name) {
  size_t size = s.size() + 1;
  ScopedMessage message;
  EXPECT_EQ(OK, node_map[0]->AllocMessage(size, 1, &message));
  memcpy(message->mutable_payload_bytes(), s.data(), size);
  message->mutable_ports()[0] = port_name;
  return message;
}

static ScopedMessage NewStringMessageWithPort(const std::string& s,
                                              const PortRef& port) {
  return NewStringMessageWithPort(s, port.name());
}

static const char* ToString(const ScopedMessage& message) {
  return static_cast<const char*>(message->payload_bytes());
}

class TestNodeDelegate : public NodeDelegate {
 public:
  explicit TestNodeDelegate(const NodeName& node_name)
      : node_name_(node_name),
        drop_messages_(false),
        read_messages_(true),
        save_messages_(false) {
  }

  void set_drop_messages(bool value) { drop_messages_ = value; }
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
    port_name->v1 = next_port_name++;
    port_name->v2 = 0;
  }

  void AllocMessage(size_t num_header_bytes,
                    size_t num_payload_bytes,
                    size_t num_ports,
                    ScopedMessage* message) override {
    message->reset(
        new TestMessage(num_header_bytes, num_payload_bytes, num_ports));
  }

  void ForwardMessage(const NodeName& node_name,
                      ScopedMessage message) override {
    if (drop_messages_) {
      DVLOG(1) << "Dropping ForwardMessage from node "
               << node_name_ << " to " << node_name;
      return;
    }
    DVLOG(1) << "ForwardMessage from node "
             << node_name_ << " to " << node_name;
    task_queue.push(new Task(node_name, std::move(message)));
  }

  void PortStatusChanged(const PortRef& port) override {
    DVLOG(1) << "PortStatusChanged for " << port.name() << "@" << node_name_;
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
        for (size_t i = 0; i < message->num_ports(); ++i) {
          std::stringstream buf;
          buf << "got port: " << message->ports()[i];

          PortRef received_port;
          node->GetPort(message->ports()[i], &received_port);

          node->SendMessage(received_port, NewStringMessage(buf.str()));

          // Avoid leaking these ports.
          node->ClosePort(received_port);
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
  bool drop_messages_;
  bool read_messages_;
  bool save_messages_;
};

class PortsTest : public testing::Test {
 public:
  PortsTest() {
    SetNode(NodeName(0, 1), nullptr);
    SetNode(NodeName(1, 1), nullptr);
  }

  ~PortsTest() override {
    DiscardPendingTasks();
    SetNode(NodeName(0, 1), nullptr);
    SetNode(NodeName(1, 1), nullptr);
  }
};

TEST_F(PortsTest, Basic1) {
  NodeName node0_name(0, 1);
  TestNodeDelegate node0_delegate(node0_name);
  Node node0(node0_name, &node0_delegate);
  SetNode(node0_name, &node0);

  NodeName node1_name(1, 1);
  TestNodeDelegate node1_delegate(node1_name);
  Node node1(node1_name, &node1_delegate);
  SetNode(node1_name, &node1);

  // Setup pipe between node0 and node1.
  PortRef x0, x1;
  EXPECT_EQ(OK, node0.CreateUninitializedPort(&x0));
  EXPECT_EQ(OK, node1.CreateUninitializedPort(&x1));
  EXPECT_EQ(OK, node0.InitializePort(x0, node1_name, x1.name()));
  EXPECT_EQ(OK, node1.InitializePort(x1, node0_name, x0.name()));

  // Transfer a port from node0 to node1.
  PortRef a0, a1;
  EXPECT_EQ(OK, node0.CreatePortPair(&a0, &a1));
  EXPECT_EQ(OK, node0.SendMessage(x0, NewStringMessageWithPort("hello", a1)));

  EXPECT_EQ(OK, node0.ClosePort(a0));

  EXPECT_EQ(OK, node0.ClosePort(x0));
  EXPECT_EQ(OK, node1.ClosePort(x1));
}

TEST_F(PortsTest, Basic2) {
  NodeName node0_name(0, 1);
  TestNodeDelegate node0_delegate(node0_name);
  Node node0(node0_name, &node0_delegate);
  SetNode(node0_name, &node0);

  NodeName node1_name(1, 1);
  TestNodeDelegate node1_delegate(node1_name);
  Node node1(node1_name, &node1_delegate);
  SetNode(node1_name, &node1);

  // Setup pipe between node0 and node1.
  PortRef x0, x1;
  EXPECT_EQ(OK, node0.CreateUninitializedPort(&x0));
  EXPECT_EQ(OK, node1.CreateUninitializedPort(&x1));
  EXPECT_EQ(OK, node0.InitializePort(x0, node1_name, x1.name()));
  EXPECT_EQ(OK, node1.InitializePort(x1, node0_name, x0.name()));

  PortRef b0, b1;
  EXPECT_EQ(OK, node0.CreatePortPair(&b0, &b1));
  EXPECT_EQ(OK, node0.SendMessage(x0, NewStringMessageWithPort("hello", b1)));
  EXPECT_EQ(OK, node0.SendMessage(b0, NewStringMessage("hello again")));

  // This may cause a SendMessage(b1) failure.
  EXPECT_EQ(OK, node0.ClosePort(b0));

  EXPECT_EQ(OK, node0.ClosePort(x0));
  EXPECT_EQ(OK, node1.ClosePort(x1));
}

TEST_F(PortsTest, Basic3) {
  NodeName node0_name(0, 1);
  TestNodeDelegate node0_delegate(node0_name);
  Node node0(node0_name, &node0_delegate);
  SetNode(node0_name, &node0);

  NodeName node1_name(1, 1);
  TestNodeDelegate node1_delegate(node1_name);
  Node node1(node1_name, &node1_delegate);
  SetNode(node1_name, &node1);

  // Setup pipe between node0 and node1.
  PortRef x0, x1;
  EXPECT_EQ(OK, node0.CreateUninitializedPort(&x0));
  EXPECT_EQ(OK, node1.CreateUninitializedPort(&x1));
  EXPECT_EQ(OK, node0.InitializePort(x0, node1_name, x1.name()));
  EXPECT_EQ(OK, node1.InitializePort(x1, node0_name, x0.name()));

  // Transfer a port from node0 to node1.
  PortRef a0, a1;
  EXPECT_EQ(OK, node0.CreatePortPair(&a0, &a1));
  EXPECT_EQ(OK, node0.SendMessage(x0, NewStringMessageWithPort("hello", a1)));
  EXPECT_EQ(OK, node0.SendMessage(a0, NewStringMessage("hello again")));

  // Transfer a0 as well.
  EXPECT_EQ(OK, node0.SendMessage(x0, NewStringMessageWithPort("foo", a0)));

  PortRef b0, b1;
  EXPECT_EQ(OK, node0.CreatePortPair(&b0, &b1));
  EXPECT_EQ(OK, node0.SendMessage(x0, NewStringMessageWithPort("bar", b1)));
  EXPECT_EQ(OK, node0.SendMessage(b0, NewStringMessage("baz")));

  // This may cause a SendMessage(b1) failure.
  EXPECT_EQ(OK, node0.ClosePort(b0));

  EXPECT_EQ(OK, node0.ClosePort(x0));
  EXPECT_EQ(OK, node1.ClosePort(x1));
}

TEST_F(PortsTest, LostConnectionToNode1) {
  NodeName node0_name(0, 1);
  TestNodeDelegate node0_delegate(node0_name);
  Node node0(node0_name, &node0_delegate);
  SetNode(node0_name, &node0);

  NodeName node1_name(1, 1);
  TestNodeDelegate node1_delegate(node1_name);
  Node node1(node1_name, &node1_delegate);
  SetNode(node1_name, &node1);

  // Setup pipe between node0 and node1.
  PortRef x0, x1;
  EXPECT_EQ(OK, node0.CreateUninitializedPort(&x0));
  EXPECT_EQ(OK, node1.CreateUninitializedPort(&x1));
  EXPECT_EQ(OK, node0.InitializePort(x0, node1_name, x1.name()));
  EXPECT_EQ(OK, node1.InitializePort(x1, node0_name, x0.name()));

  // Transfer port to node1 and simulate a lost connection to node1. Dropping
  // events from node1 is how we simulate the lost connection.

  node1_delegate.set_drop_messages(true);

  PortRef a0, a1;
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
  NodeName node0_name(0, 1);
  TestNodeDelegate node0_delegate(node0_name);
  Node node0(node0_name, &node0_delegate);
  node_map[0] = &node0;

  NodeName node1_name(1, 1);
  TestNodeDelegate node1_delegate(node1_name);
  Node node1(node1_name, &node1_delegate);
  node_map[1] = &node1;

  // Setup pipe between node0 and node1.
  PortRef x0, x1;
  EXPECT_EQ(OK, node0.CreateUninitializedPort(&x0));
  EXPECT_EQ(OK, node1.CreateUninitializedPort(&x1));
  EXPECT_EQ(OK, node0.InitializePort(x0, node1_name, x1.name()));
  EXPECT_EQ(OK, node1.InitializePort(x1, node0_name, x0.name()));

  node1_delegate.set_read_messages(false);

  PortRef a0, a1;
  EXPECT_EQ(OK, node0.CreatePortPair(&a0, &a1));
  EXPECT_EQ(OK, node0.SendMessage(x0, NewStringMessageWithPort("take a1", a1)));

  PumpTasks();

  node1_delegate.set_drop_messages(true);

  EXPECT_EQ(OK, node0.LostConnectionToNode(node1_name));

  PumpTasks();

  ScopedMessage message;
  EXPECT_EQ(ERROR_PORT_PEER_CLOSED, node0.GetMessage(a0, &message));
  EXPECT_FALSE(message);
}

TEST_F(PortsTest, GetMessage1) {
  NodeName node0_name(0, 1);
  TestNodeDelegate node0_delegate(node0_name);
  Node node0(node0_name, &node0_delegate);
  node_map[0] = &node0;

  PortRef a0, a1;
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
  NodeName node0_name(0, 1);
  TestNodeDelegate node0_delegate(node0_name);
  Node node0(node0_name, &node0_delegate);
  node_map[0] = &node0;

  node0_delegate.set_read_messages(false);

  PortRef a0, a1;
  EXPECT_EQ(OK, node0.CreatePortPair(&a0, &a1));

  EXPECT_EQ(OK, node0.SendMessage(a1, NewStringMessage("1")));

  ScopedMessage message;
  EXPECT_EQ(OK, node0.GetMessage(a0, &message));

  ASSERT_TRUE(message);
  EXPECT_EQ(0, strcmp("1", ToString(message)));

  EXPECT_EQ(OK, node0.ClosePort(a0));
  EXPECT_EQ(OK, node0.ClosePort(a1));
}

TEST_F(PortsTest, GetMessage3) {
  NodeName node0_name(0, 1);
  TestNodeDelegate node0_delegate(node0_name);
  Node node0(node0_name, &node0_delegate);
  node_map[0] = &node0;

  node0_delegate.set_read_messages(false);

  PortRef a0, a1;
  EXPECT_EQ(OK, node0.CreatePortPair(&a0, &a1));

  const char* kStrings[] = {
    "1",
    "2",
    "3"
  };

  for (size_t i = 0; i < sizeof(kStrings)/sizeof(kStrings[0]); ++i)
    EXPECT_EQ(OK, node0.SendMessage(a1, NewStringMessage(kStrings[i])));

  ScopedMessage message;
  for (size_t i = 0; i < sizeof(kStrings)/sizeof(kStrings[0]); ++i) {
    EXPECT_EQ(OK, node0.GetMessage(a0, &message));
    ASSERT_TRUE(message);
    EXPECT_EQ(0, strcmp(kStrings[i], ToString(message)));
    DVLOG(1) << "got " << kStrings[i];
  }

  EXPECT_EQ(OK, node0.ClosePort(a0));
  EXPECT_EQ(OK, node0.ClosePort(a1));
}

TEST_F(PortsTest, Delegation1) {
  NodeName node0_name(0, 1);
  TestNodeDelegate node0_delegate(node0_name);
  Node node0(node0_name, &node0_delegate);
  SetNode(node0_name, &node0);

  NodeName node1_name(1, 1);
  TestNodeDelegate node1_delegate(node1_name);
  Node node1(node1_name, &node1_delegate);
  node_map[1] = &node1;

  node0_delegate.set_save_messages(true);
  node1_delegate.set_save_messages(true);

  // Setup pipe between node0 and node1.
  PortRef x0, x1;
  EXPECT_EQ(OK, node0.CreateUninitializedPort(&x0));
  EXPECT_EQ(OK, node1.CreateUninitializedPort(&x1));
  EXPECT_EQ(OK, node0.InitializePort(x0, node1_name, x1.name()));
  EXPECT_EQ(OK, node1.InitializePort(x1, node0_name, x0.name()));

  // In this test, we send a message to a port that has been moved.

  PortRef a0, a1;
  EXPECT_EQ(OK, node0.CreatePortPair(&a0, &a1));

  EXPECT_EQ(OK, node0.SendMessage(x0, NewStringMessageWithPort("a1", a1)));

  PumpTasks();

  ScopedMessage message;
  ASSERT_TRUE(node1_delegate.GetSavedMessage(&message));

  ASSERT_EQ(1u, message->num_ports());

  // This is "a1" from the point of view of node1.
  PortName a2_name = message->ports()[0];

  EXPECT_EQ(OK, node1.SendMessage(x1, NewStringMessageWithPort("a2", a2_name)));

  PumpTasks();

  EXPECT_EQ(OK, node0.SendMessage(a0, NewStringMessage("hello")));

  PumpTasks();

  ASSERT_TRUE(node0_delegate.GetSavedMessage(&message));

  ASSERT_EQ(1u, message->num_ports());

  // This is "a2" from the point of view of node1.
  PortName a3_name = message->ports()[0];

  PortRef a3;
  EXPECT_EQ(OK, node0.GetPort(a3_name, &a3));

  EXPECT_EQ(0, strcmp("a2", ToString(message)));

  ASSERT_TRUE(node0_delegate.GetSavedMessage(&message));

  EXPECT_EQ(0u, message->num_ports());
  EXPECT_EQ(0, strcmp("hello", ToString(message)));

  EXPECT_EQ(OK, node0.ClosePort(a0));
  EXPECT_EQ(OK, node0.ClosePort(a3));

  EXPECT_EQ(OK, node0.ClosePort(x0));
  EXPECT_EQ(OK, node1.ClosePort(x1));
}

TEST_F(PortsTest, Delegation2) {
  NodeName node0_name(0, 1);
  TestNodeDelegate node0_delegate(node0_name);
  Node node0(node0_name, &node0_delegate);
  SetNode(node0_name, &node0);

  NodeName node1_name(1, 1);
  TestNodeDelegate node1_delegate(node1_name);
  Node node1(node1_name, &node1_delegate);
  node_map[1] = &node1;

  node0_delegate.set_save_messages(true);
  node1_delegate.set_save_messages(true);

  for (int i = 0; i < 10; ++i) {
    // Setup pipe a<->b between node0 and node1.
    PortRef A, B;
    EXPECT_EQ(OK, node0.CreateUninitializedPort(&A));
    EXPECT_EQ(OK, node1.CreateUninitializedPort(&B));
    EXPECT_EQ(OK, node0.InitializePort(A, node1_name, B.name()));
    EXPECT_EQ(OK, node1.InitializePort(B, node0_name, A.name()));

    PortRef C, D;
    EXPECT_EQ(OK, node0.CreatePortPair(&C, &D));

    PortRef E, F;
    EXPECT_EQ(OK, node0.CreatePortPair(&E, &F));

    // Pass D over A to B.
    EXPECT_EQ(OK, node0.SendMessage(A, NewStringMessageWithPort("1", D)));

    // Pass F over C to D.
    EXPECT_EQ(OK, node0.SendMessage(C, NewStringMessageWithPort("1", F)));

    // This message should find its way to node1.
    EXPECT_EQ(OK, node0.SendMessage(E, NewStringMessage("hello")));

    PumpTasks();

    for (;;) {
      ScopedMessage message;
      if (node1_delegate.GetSavedMessage(&message)) {
        if (strcmp("hello", ToString(message)) == 0)
          break;
      } else {
        ASSERT_TRUE(false);  // "hello" message not delivered!
        break;
      }
    }
  }
}

TEST_F(PortsTest, SendUninitialized1) {
  NodeName node0_name(0, 1);
  TestNodeDelegate node0_delegate(node0_name);
  Node node0(node0_name, &node0_delegate);
  node_map[0] = &node0;

  NodeName node1_name(1, 1);
  TestNodeDelegate node1_delegate(node1_name);
  Node node1(node1_name, &node1_delegate);
  node_map[1] = &node1;

  // Begin to setup a pipe between node0 and node1, but don't initialize either
  // endpoint.
  PortRef x0, x1;
  EXPECT_EQ(OK, node0.CreateUninitializedPort(&x0));
  EXPECT_EQ(OK, node1.CreateUninitializedPort(&x1));

  node0_delegate.set_save_messages(true);
  node1_delegate.set_save_messages(true);

  // Send a message on each port and expect neither to arrive yet.

  EXPECT_EQ(OK, node0.SendMessage(x0, NewStringMessage("it can wait")));
  EXPECT_EQ(OK, node1.SendMessage(x1, NewStringMessage("hello eventually")));

  PumpTasks();

  ScopedMessage message;
  EXPECT_FALSE(node0_delegate.GetSavedMessage(&message));
  EXPECT_FALSE(node1_delegate.GetSavedMessage(&message));

  // Initialize the ports and expect both messages to arrive.

  EXPECT_EQ(OK, node0.InitializePort(x0, node1_name, x1.name()));
  EXPECT_EQ(OK, node1.InitializePort(x1, node0_name, x0.name()));

  PumpTasks();

  ASSERT_TRUE(node0_delegate.GetSavedMessage(&message));
  EXPECT_EQ(0, strcmp("hello eventually", ToString(message)));

  ASSERT_TRUE(node1_delegate.GetSavedMessage(&message));
  EXPECT_EQ(0, strcmp("it can wait", ToString(message)));
}

TEST_F(PortsTest, SendUninitialized2) {
  NodeName node0_name(0, 1);
  TestNodeDelegate node0_delegate(node0_name);
  Node node0(node0_name, &node0_delegate);
  node_map[0] = &node0;

  NodeName node1_name(1, 1);
  TestNodeDelegate node1_delegate(node1_name);
  Node node1(node1_name, &node1_delegate);
  node_map[1] = &node1;

  // Begin to setup a pipe between node0 and node1, but don't initialize either
  // endpoint.
  PortRef x0, x1;
  EXPECT_EQ(OK, node0.CreateUninitializedPort(&x0));
  EXPECT_EQ(OK, node1.CreateUninitializedPort(&x1));

  node0_delegate.set_save_messages(true);
  node1_delegate.set_save_messages(true);

  PortRef A, B;
  EXPECT_EQ(OK, node0.CreatePortPair(&A, &B));

  // Send B over the uninitialized x0 port and expect it to not yet be received.

  EXPECT_EQ(OK, node0.SendMessage(x0, NewStringMessageWithPort("hi", B)));

  PumpTasks();

  ScopedMessage message;
  EXPECT_FALSE(node1_delegate.GetSavedMessage(&message));

  // Send a message over A to B while B is waiting to be sent to x1.
  EXPECT_EQ(OK, node0.SendMessage(A, NewStringMessage("hey")));

  PumpTasks();

  // Nothing should have arrived yet because B should be buffering, waiting to
  // proxy to its new destination wherever that may be.
  EXPECT_FALSE(node1_delegate.GetSavedMessage(&message));

  // Initialize the ports and expect both messages to arrive.

  EXPECT_EQ(OK, node0.InitializePort(x0, node1_name, x1.name()));
  EXPECT_EQ(OK, node1.InitializePort(x1, node0_name, x0.name()));

  PumpTasks();

  ASSERT_TRUE(node1_delegate.GetSavedMessage(&message));
  EXPECT_EQ(0, strcmp("hi", ToString(message)));

  PortRef received_port;
  node1.GetPort(message->ports()[0], &received_port);

  ASSERT_TRUE(node1_delegate.GetSavedMessage(&message));
  EXPECT_EQ(0, strcmp("hey", ToString(message)));

  // Send a message over the previously received port and expect it to get back
  // to A.

  EXPECT_EQ(OK, node1.SendMessage(received_port, NewStringMessage("bye")));

  PumpTasks();

  ASSERT_TRUE(node0_delegate.GetSavedMessage(&message));
  EXPECT_EQ(0, strcmp("bye", ToString(message)));
}

}  // namespace test
}  // namespace ports
}  // namespace edk
}  // namespace mojo
