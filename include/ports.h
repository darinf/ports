#ifndef PORTS_H_
#define PORTS_H_

#include <stdint.h>

namespace ports {

typedef uint64_t PortName;
typedef uint64_t NodeName;

struct Message {
  uint32_t sequence_num;
  const void* bytes;
  size_t num_bytes;
  const PortName* dependent_ports;
  size_t num_dependent_ports;
};

Message* AllocMessage(
    size_t num_bytes,
    size_t num_dependent_ports);

void FreeMessage(
    Message* message);

class NodeDelegate {
 public:
  virtual int Send_AcceptMessage)(
      NodeName to_node,
      PortName port,
      Message* message) = 0;

  virtual int Send_AcceptPortAck)(
      NodeName to_node,
      PortName port) = 0;

  virtual int Send_UpdatePort)(
      NodeName to_node,
      PortName port,
      NodeName peer_node) = 0;

  virtual int Send_UpdatePortAck)(
      NodeName to_node,
      PortName port) = 0;

  virtual int Send_PeerClosed)(
      NodeName to_node,
      PortName port) = 0;

  virtual int MessagesAvailable)(
      PortName port) = 0;
};

class Node {
 public:
  explicit Node(NodeDelegate* delegate);
  ~Node();

  int GetMessage(
      PortName port,
      Message** message);

  int SendMessage(
      PortName port,
      Message* message); 

  int AcceptMessage(
      PortName port,
      Message* message);

  int AcceptPort(
      PortName port,
      PortName peer,
      NodeName peer_node,
      uint32_t next_sequence_num);

  int AcceptPortAck(
      PortName port);

  int UpdatePort(
      PortName port,
      NodeName peer_node);

  int UpdatePortAck(
      PortName port);

  int PeerClosed(
      PortName port);

 private:
  struct State;
  State* state_;
};

}  // namespace ports

#endif  // PORTS_H_
