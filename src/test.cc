#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <queue>

#include "../include/ports.h"

namespace ports {
namespace test {

struct Task {
  enum Type {
    kAcceptMessage,
    kAcceptMessageAck,
    kUpdatePort,
    kUpdatePortAck,
    kPeerClosed
  } type;

  NodeName to_node;
  PortName port;
  std::unique_ptr<Message> message;
  uint32_t sequence_num;
  PortName new_peer;
  NodeName new_peer_node;
};

static std::queue<Task*> task_queue;
static Node* node_map[2];

static void DoTask(Task* task) {
  Node* node = node_map[task->to_node.value];
  switch (task->type) {
    case Task::kAcceptMessage:
      node->AcceptMessage(task->port, task->message.release());
      break;
    case Task::kAcceptMessageAck:
      node->AcceptMessageAck(task->port, task->sequence_num);
      break;
    case Task::kUpdatePort:
      node->UpdatePort(task->port, task->new_peer, task->new_peer_node);
      break;
    case Task::kUpdatePortAck:
      node->UpdatePortAck(task->port);
      break;
    case Task::kPeerClosed:
      node->PeerClosed(task->port);
      break;
  }
}

static void PumpTasks() {
  while (!task_queue.empty()) {
    Task* task = task_queue.front();
    task_queue.pop();
    DoTask(task);
    delete task;
  }
}

static Message* NewStringMessage(const char* s) {
  size_t len = strlen(s) + 1;
  Message* message = AllocMessage(len, 0); 
  memcpy(message->bytes, s, len);
  return message;
}

static Message* NewStringMessageWithPort(const char* s, PortName port) {
  size_t len = strlen(s) + 1;
  Message* message = AllocMessage(len, 1); 
  memcpy(message->bytes, s, len);
  message->ports[0].name = port;
  return message;
}

static void PrintMessage(const Message* message) {
  printf(":[seq=%u]\"%s\"",
      message->sequence_num, static_cast<const char*>(message->bytes));
  for (size_t i = 0; i < message->num_ports; ++i)
    printf(":%lX", message->ports[i].name.value);
}

class TestNodeDelegate : public NodeDelegate {
 public:
  explicit TestNodeDelegate(NodeName node) : node_(node) {
  }

  virtual void Send_AcceptMessage(
      NodeName to_node,
      PortName port,
      Message* message) {
    printf("n%lX:Send_AcceptMessage(n%lX,p%lX)",
        node_.value, to_node.value, port.value);
    PrintMessage(message);
    printf("\n");

    Task* task = new Task();
    task->type = Task::kAcceptMessage;
    task->to_node = to_node;
    task->port = port;
    task->message.reset(message);
    task_queue.push(task);
  }

  virtual void Send_AcceptMessageAck(
      NodeName to_node,
      PortName port,
      uint32_t sequence_num) {
    printf("n%lX:Send_AcceptMessageAck(n%lX,p%lX,seq=%u)\n",
        node_.value, to_node.value, port.value, sequence_num);

    Task* task = new Task();
    task->type = Task::kAcceptMessageAck;
    task->to_node = to_node;
    task->port = port;
    task->sequence_num = sequence_num;
    task_queue.push(task);
  }

  virtual void Send_UpdatePort(
      NodeName to_node,
      PortName port,
      PortName new_peer,
      NodeName new_peer_node) override {
    printf("n%lX:Send_UpdatePort(n%lX,p%lX,p%lX,n%lX)\n", 
        node_.value, to_node.value, port.value, new_peer.value,
        new_peer_node.value);

    Task* task = new Task();
    task->type = Task::kUpdatePort;
    task->to_node = to_node;
    task->port = port;
    task->new_peer = new_peer;
    task->new_peer_node = new_peer_node;
    task_queue.push(task);
  }

  virtual void Send_UpdatePortAck(NodeName to_node, PortName port) override {
    printf("n%lX:Send_UpdatePortAck(n%lX,p%lX)\n",
        node_.value, to_node.value, port.value);

    Task* task = new Task();
    task->type = Task::kUpdatePortAck;
    task->to_node = to_node;
    task->port = port;
    task_queue.push(task);
  }

  virtual void Send_PeerClosed(NodeName to_node, PortName port) override {
    printf("n%lX:Send_PeerClosed(n%lX,p%lX)\n",
        node_.value, to_node.value, port.value);

    Task* task = new Task();
    task->type = Task::kPeerClosed;
    task->to_node = to_node;
    task->port = port;
    task_queue.push(task);
  }

  virtual void MessagesAvailable(PortName port) override {
    for (;;) {
      Message* message;
      if (node_map[node_.value]->GetMessage(port, &message) != OK || !message)
        break;
      printf("n%lX:MessagesAvailable(p%lX)", node_.value, port.value);
      PrintMessage(message);
      printf("\n");
      for (size_t i = 0; i < message->num_ports; ++i) {
        node_map[node_.value]->SendMessage(message->ports[i].name,
                                           NewStringMessage("got port"));
      }
      FreeMessage(message);
    }
  }

  virtual PortName GeneratePortName() override {
    static uint64_t next_port_name = 1;
    printf("n%lX:GeneratePortName => p%lX\n", node_.value, next_port_name);
    return PortName(next_port_name++);
  }

 private:
  NodeName node_;
};

static void RunTest() {
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
  x0 = node0_delegate.GeneratePortName();
  x1 = node1_delegate.GeneratePortName();
  node0.AddPort(x0, x1, node1_name);
  node1.AddPort(x1, x0, node0_name);

  // Transfer a message from node0 to node1.
  node0.SendMessage(x0, NewStringMessage("hello world"));

  // Transfer a port from node0 to node1.
  PortName a0, a1;
  node0.CreatePortPair(&a0, &a1);
  node0.SendMessage(x0, NewStringMessageWithPort("take port", a1));

  PumpTasks();
}

}  // namespace test
}  // namespace ports

int main(int argc, char** argv) {
  ports::test::RunTest();
  return 0;
}
