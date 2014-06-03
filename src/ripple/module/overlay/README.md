# Overlay

## Introduction

The _Ripple payment network_ consists of a collection of _peers_ running the
**rippled software**. Each peer maintains multiple outgoing connections and
optional incoming connections to other peers. These connections are made over
both the public Internet and private local area networks. This network defines
a fully connected directed graph of nodes. Peers send and receive messages to
other connected peers. This peer to peer network, layered on top of the public
and private Internet, forms an [_overlay network_][overlay_network]. The
contents of the messages and the behavior of peers in response to the messages,
plus the information exchanged during the handshaking phase of connection
establishment, defines the _Peer Protocol_.

[overlay_network]: http://en.wikipedia.org/wiki/Overlay_network