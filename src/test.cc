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
}

static void PumpTasks() {
  while (!task_queue.empty()) {
    Task* task = task_queue.front();
    task_queue.pop();
    DoTask(task);
    delete task;
  }
}

class TestNodeDelegate : public NodeDelegate {
 public:
  explicit TestNodeDelegate(NodeName node) : node_(node), next_port_name_(1) {
  }

  virtual void Send_AcceptMessage(
      NodeName to_node,
      PortName port,
      Message* message) {
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
  }

  virtual void Send_UpdatePort(
      NodeName to_node,
      PortName port,
      PortName new_peer,
      NodeName new_peer_node) override {
  }

  virtual void Send_UpdatePortAck(NodeName to_node, PortName port) override {
  }

  virtual void Send_PeerClosed(NodeName to_node, PortName port) override {
  }

  virtual void MessagesAvailable(PortName port) override {
  }

  virtual PortName GeneratePortName() override {
    return PortName(next_port_name_++);
  }

 private:
  NodeName node_;
  uint64_t next_port_name_;
};

static void RunTest() {
  TestNodeDelegate test_delegate1(NodeName(1));
  Node node1(NodeName(1), &test_delegate1);
  node_map[0] = &node1;

  TestNodeDelegate test_delegate2(NodeName(2));
  Node node2(NodeName(2), &test_delegate2);
  node_map[1] = &node2;

  PumpTasks();
}

}  // namespace test
}  // namespace ports

int main(int argc, char** argv) {
  ports::test::RunTest();
  return 0;
}
