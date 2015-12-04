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
has a pointer to the next port, called its peer. In the simplest form this is
a cycle like so:

```
A --> B --> A
```

When a port is moved to another node via `SendMessage`, what really happens
under the hood is another port is created at the new node and inserted into the 
list. If port `B` is moved to another node, then you end up with port `C` being
created at the other node, and the resulting circular list looks like:

```
A --> B --> C --> A
```
