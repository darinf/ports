# Ports

## Overview

Ports is a simple messaging system. In this system, ports come in pairs, and
can be used for bi-directional communication. Messages are addressed to ports.
When a message is sent from a port, it is addressed to and received by the
port's peer.

Ports are bound to nodes. Nodes can be thought of as different entities (e.g.,
processes) that wish to communicate. A node can have many ports, but those
ports can be transferred to other nodes.

The ports library is not concerned with low-level transport details. Rather it
handles the routing logic and complexity around transferring ports. The
embedder of this library provides the low-level transport. The low-level
transport is not expected to maintain any magic ordering. Indeed, the transport
of messages between nodes can happen out of order. The ports library is designed
explicitly to allow for that.

To use the library, allocate one or more Node objects. Provide a Node object
with a NodeDelegate. This provides the Node with a means to communicate with
the outside world and lets the embedder provide low-level transport and to
observe when messages arrive at the node for handling.

The ports library is thread safe, and a NodeDelegate may be invoked from any
thread.

## Implementation details

Ports can be thought of as items in a circular, singly linked list. Every port
has a pointer to the next port, called its peer. This is the only port it can
send messages to. In the simplest case of two connected ports A and B this
forms a cycle like so:

```
A --> B --> A
```

When a port is moved to another node via `SendMessage`, what really happens
under the hood is another port is created at the new node and inserted into the
list. For example, if port B is moved to another node, then you end up with
port C being created at the other node, and the resulting circular list looks
like this:

```
A --> B --> C --> A
```

Here, port B is just a forwarding port called a proxy. Messages are not
delivered at port B, rather they are forwarded to port C. The embedding
application does not see the existence of port B, and indeed port B upon
becoming a proxy has a short lifespan. Once a port has become a proxy, its
next step is to be eliminated. Port A should just send its messages directly
to port C.

To remove itself, port B does not try to directly talk to the port sending it
messages. It doesn't have a pointer to A, so instead it forwards a message to
its peer, port C, announcing that port B is a proxy to port C. Port C is not
interested in this information but is happy to forward the message along.  Port
C forwards the message to its peer, port A. Port A receives the message, and
can act on it to change its pointer to port C. As port A knows that its old
peer was port B, port A can send port B a final last message indicating that
its annoucement about being a proxy has been acknowledged. Now, port B is ready
to remove itself.

The last step for port B before removing itself is to ensure that any
outstanding messages from port A have been forwarded to port C. Once that is
done, port B can remove itself.

To help simplify the implementation, all messages are assigned sequence numbers
by the sending port. The sequence numbers increase incrementally by 1, and the
recipient port uses the sequence numbers to ensure proper ordering of the
received messages.

When acknowledging a port's annoucement that it has become a proxy port, the
sequence number of the last message sent to the proxy port is sent to the proxy
port. This allows the proxy port to observe when it has received the last
message it will receive.

## TODO: Figure out shutdown
