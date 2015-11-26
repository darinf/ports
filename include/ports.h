#ifndef PORTS_H_
#define PORTS_H_

#include <stdint.h>

namespace ports {

typedef uint64_t PortName;
typedef uint64_t NodeName;

struct Context;

struct Message {
  uint32_t sequence_num;
  const void* bytes;
  size_t num_bytes;
  const PortName* dependent_ports;
  size_t num_dependent_ports;
};

struct Callbacks {
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

int Initialize(
    Callbacks* callbacks,
    Context** context);

int Shutdown(
    Context* context);

int AllocMessage(
    Context* context,
    size_t num_bytes,
    size_t num_dependent_ports,
    Message** message);

int FreeMessage(
    Context* context,
    Message* message);

int SendMessage(
    Context* context,
    PortName port,
    Message* message); 

int GetMessage(
    Context* context,
    PortName port,
    Message** message);

int AcceptMessage(
    Context* context,
    PortName port,
    Message* message);

int AcceptPort(
    Context* context,
    PortName port,
    PortName peer,
    NodeName peer_node,
    uint32_t next_sequence_num);

int AcceptPortAck(
    Context* context,
    PortName port);

int UpdatePort(
    Context* context,
    PortName port,
    NodeName peer_node);

int UpdatePortAck(
    Context* context,
    PortName port);

int PeerClosed(
    Context* context,
    PortName port);

}  // namespace ports

#endif  // PORTS_H_
