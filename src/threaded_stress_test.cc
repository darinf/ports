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

#include <atomic>
#include <condition_variable>
#include <thread>
#include <memory>
#include <mutex>
#include <queue>

#include "ports/include/ports.h"
#include "ports/src/logging.h"

#include <gtest/gtest.h>

// Each thread has a thread-local mapping from thread_id to port to used to
// communication to the thread.
//
//   TLS: map(thread_id) -> port
//
// This can be used to send a message from any thread to any thread.
//
// Upon receiving a message, a thread will wake up and perform a random
// activity. Activities include:
//
//   1. Forward the message to a random thread.
//   2. Forward the received ports to a random assortment of threads.
//   3  Send a message with a random number of ports to the received ports,
//      send the peer ports to other random threads, and then close the
//      received ports.
//   4. Close the received ports. Send a new message to a random port.
//
// The test concludes after N messages have been received.

namespace ports {

static const size_t kNumThreads = 100;
static const size_t kNumNodes = 10;

static const uint32_t kMaxMessages = 1000000;
static std::atomic<uint32_t> message_counter(1);
static bool ShouldExit() {
  return message_counter.load() > kMaxMessages;
}
static void MessageWasRead() {
  uint32_t value = message_counter.fetch_add(1);
  if (value % (kMaxMessages / 1000) == 0) {
    printf(".");
    fflush(stdout);
  }
  if (value % (kMaxMessages / 10) == 0)
    printf("\n");
}

struct NodeData {
  std::unique_ptr<Node> node;
  std::unique_ptr<NodeDelegate> delegate;
  std::mutex lock;
  std::queue<Event> events;
  std::condition_variable cvar;
};

struct ThreadData {
  NodeName node_name;
  PortName ports[kNumThreads];  // Ports to all of the other threads.
  PortName self_port;           // The peer of the port to ourselves.
};
static thread_local ThreadData* this_thread_data;

static void PutEvent(NodeData* node_data, Event event) {
  {
    std::lock_guard<std::mutex> guard(node_data->lock);
    node_data->events.emplace(std::move(event));
  }
  node_data->cvar.notify_one();
}

static bool GetEvent(NodeData* node_data, Event* event) {
  while (!ShouldExit()) {
    std::unique_lock<std::mutex> guard(node_data->lock);
    if (node_data->events.empty()) {
      node_data->cvar.wait(guard);
    } else {
      *event = std::move(node_data->events.front());
      node_data->events.pop();
      return true;
    }
  }
  return false;
}

static std::atomic<uint64_t> next_unique_uint64;
static uint64_t GenerateUniqueUint64() {
  return next_unique_uint64.fetch_add(1, std::memory_order_relaxed);
}

static ThreadData* kThreadData[kNumThreads];
static NodeData* kNodeData[kNumNodes];

static int RandomDestination() {
  return rand() % kNumThreads;
}

static ScopedMessage NewMessageWithNumPorts(size_t num_ports) {
  const char kMessageText[] = "hello";
  ScopedMessage message(AllocMessage(sizeof(kMessageText), num_ports));
  strcpy(static_cast<char*>(message->bytes), kMessageText);
  return std::move(message);
}

static ScopedMessage NewMessageWithPort(PortName port) {
  ScopedMessage message(NewMessageWithNumPorts(1));
  message->ports[0].name = port;
  return std::move(message);
}

static ScopedMessage NewMessageWithRandomNumPorts(
    Node* node, std::vector<PortName>* other_ports) {
  size_t num_ports = rand() % 10;

  ScopedMessage message(NewMessageWithNumPorts(num_ports));

  other_ports->clear();
  for (size_t i = 0; i < num_ports; ++i) {
    PortName a, b;
    node->CreatePortPair(&a, &b);
    message->ports[i].name = a;
    other_ports->push_back(b);
  }

  return std::move(message);
}

static void DoRandomActivity(Node* node, ScopedMessage message) {
  switch (rand() % 3) {
    case 0: {
      // Forward the message to a random thread.
      node->SendMessage(this_thread_data->ports[RandomDestination()],
                        std::move(message));
      break;
    }
    case 1:
      // Forward the received ports to a random assortment of threads.
      for (size_t i = 0; i < message->num_ports; ++i) {
        ScopedMessage new_message = NewMessageWithPort(message->ports[i].name);
        node->SendMessage(this_thread_data->ports[RandomDestination()],
                          std::move(new_message));
      }
      break;
    case 2:
      // Send a message with a random number of ports to the received ports,
      // send the peer ports to other random threads, and then close the
      // received ports.
      for (size_t i = 0; i < message->num_ports; ++i) {
        std::vector<PortName> other_ports;
        ScopedMessage new_message(
            NewMessageWithRandomNumPorts(node, &other_ports));
        for (size_t i = 0; i < other_ports.size(); ++i) {
          node->SendMessage(this_thread_data->ports[RandomDestination()],
                            NewMessageWithPort(other_ports[i]));
        }
        node->SendMessage(message->ports[i].name, std::move(new_message));
        node->ClosePort(message->ports[i].name);
      }
      break;
    case 3:
      // Close the received ports. Send a new message to a random port.
      for (size_t i = 0; i < message->num_ports; ++i)
        node->ClosePort(message->ports[i].name);
      node->SendMessage(this_thread_data->ports[RandomDestination()],
                        NewMessageWithNumPorts(0));
      break;
  }
}

class TestNodeDelegate : public NodeDelegate {
 public:
  explicit TestNodeDelegate(const NodeName& node_name)
      : node_name_(node_name) {
  }

  virtual void GenerateRandomPortName(PortName* port_name) override {
    port_name->value_major = GenerateUniqueUint64();
    port_name->value_minor = 0;
  }

  virtual void SendEvent(const NodeName& node_name, Event event) override {
    PutEvent(kNodeData[node_name.value_major], std::move(event));
  }

  virtual void MessagesAvailable(const PortName& port) override {
    Node* node = kNodeData[node_name_.value_major]->node.get();
    for (;;) {
      ScopedMessage message;
      node->GetMessage(port, &message);
      if (message) {
        DoRandomActivity(node, std::move(message));
        MessageWasRead();
      } else {
        break;
      }
    }
  }
  
 private:
  NodeName node_name_;
};

static void ThreadFunc(size_t thread_index) {
  this_thread_data = kThreadData[thread_index];
  
  NodeData* node_data = kNodeData[this_thread_data->node_name.value_major];

  for (;;) {
    Event event(Event::kAcceptMessage);  // dummy event
    if (GetEvent(node_data, &event)) {
      node_data->node->AcceptEvent(std::move(event));
    } else {
      break;
    }
  }
}

static void KickOffTest() {
  ThreadData* thread_data = kThreadData[0];
  NodeData* node_data = kNodeData[thread_data->node_name.value_major];

  std::vector<PortName> other_ports;
  ScopedMessage message(
      NewMessageWithRandomNumPorts(node_data->node.get(), &other_ports));
  for (size_t i = 0; i < other_ports.size(); ++i) {
    node_data->node->SendMessage(
        thread_data->ports[RandomDestination()],
        NewMessageWithPort(other_ports[i]));
  }
  node_data->node->SendMessage(
      thread_data->ports[RandomDestination()], std::move(message));
}

TEST(ThreadedStressTest, RandomDance) {
  for (size_t i = 0; i < kNumNodes; ++i) {
    NodeName node_name(i, 0);
    kNodeData[i] = new NodeData();
    kNodeData[i]->delegate.reset(new TestNodeDelegate(node_name));
    kNodeData[i]->node.reset(new Node(node_name, kNodeData[i]->delegate.get()));
  }

  for (size_t i = 0; i < kNumThreads; ++i) {
    kThreadData[i] = new ThreadData();
    kThreadData[i]->node_name = NodeName(i % kNumNodes, 0);
  }

  // Create web of ports between threads.
  for (size_t i = 0; i < kNumThreads; ++i) {
    for (size_t j = i; j < kNumThreads; ++j) {
      NodeName node_name_i = kThreadData[i]->node_name;
      NodeName node_name_j = kThreadData[j]->node_name;

      Node* node_i = kNodeData[node_name_i.value_major]->node.get();
      Node* node_j = kNodeData[node_name_j.value_major]->node.get();

      PortName port_i, port_j;
      node_i->CreatePort(&port_i);
      node_j->CreatePort(&port_j);
      node_i->InitializePort(port_i, node_name_j, port_j);
      node_j->InitializePort(port_j, node_name_i, port_i);

      if (i == j) {
        kThreadData[i]->ports[j] = port_i;
        kThreadData[i]->self_port = port_j;
      } else {
        kThreadData[i]->ports[j] = port_i;
        kThreadData[j]->ports[i] = port_j;
      }
    }
  }

  std::thread threads[kNumThreads];

  for (size_t i = 0; i < kNumThreads; ++i)
    threads[i] = std::move(std::thread(ThreadFunc, i));

  KickOffTest();

  for (size_t i = 0; i < kNumThreads; ++i)
    threads[i].join();

  for (size_t i = 0; i < kNumThreads; ++i)
    delete kThreadData[i];

  for (size_t i = 0; i < kNumNodes; ++i)
    delete kNodeData[i];
}

}  // namespace ports

