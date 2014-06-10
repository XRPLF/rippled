# Overlay

## Introduction

The _Ripple payment network_ consists of a collection of _peers_ running
**rippled**. Each peer maintains multiple outgoing connections and optional
incoming connections to other peers. These connections are made over both
the public Internet and private local area networks. This network defines a
fully connected directed graph of nodes where vertices are instances of rippled
and edges are persistent TCP/IP connections. Peers send and receive messages to
other connected peers. This peer to peer network, layered on top of the public
and private Internet, forms an [_overlay network_][overlay_network]. The
contents of the messages and the behavior of peers in response to the messages,
plus the information exchanged during the handshaking phase of connection
establishment, defines the _Ripple peer protocol_ (_protocol_ in this context).

## Overview

Each connection is represented by a _Peer_ object. The Overlay manager
establishes, receives, and maintains connections to peers. Protocol
messages are exchanged between peers and serialized using
[_Google Protocol Buffers_][protocol_buffers].

[overlay_network]: http://en.wikipedia.org/wiki/Overlay_network
[protocol_buffers]: https://developers.google.com/protocol-buffers/
