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

### Structure

Each connection between peers is identified by its connection type, which
affects the behavior of message routing:

* Leaf

* Peer

## Roles

Depending on the type of connection desired, the peers will modify their
behavior according to certain roles:

### Leaf or Superpeer

A peer in the leaf role does not route messages. In the superpeer role, a
peer accepts incoming connections from other leaves and superpeers up to the
configured slot limit. It also routes messages. For a particular connection,
the choice of leaf or superpeer is mutually exclusive. However, a peer can
operate in both the leaf and superpeer role for different connections. One of
the requirements 

### Client Handler

While not part of the responsibilities of the Overlay module, a peer
operating in the Client Handler role accepts incoming connections from clients
and services them through the JSON-RPC interface. A peer can operate in either
the leaf or superpeer roles while also operating as a client handler.




## Handshake

To establish a protocol connection, a peer makes an outgoing TLS encrypted
connection to a remote peer, then sends a HTTP request with no message body.
The request uses the [_HTTP/1.1 Upgrade_][upgrade_header] mechanism with some
custom fields to communicate protocol specific information:

```
GET / HTTP/1.1
User-Agent: Ripple-0.26.0
Local-Address: 192.168.0.101:8421
Remote-Address: 208.239.76.97:51234
Upgrade: Ripple/1.2, Ripple/1.3
Connection: Upgrade
Connect-As: Leaf, Peer
Accept-Encoding: identity, zlib, snappy
Protocol-Public-Key: aBRoQibi2jpDofohooFuzZi9nEzKw9Zdfc4ExVNmuXHaJpSPh8uJ
Protocol-Session-Cookie: 71ED064155FFADFA38782C5E0158CB26
```

Upon receipt of a well-formed HTTP request the remote peer will send back a
HTTP response indicating the connection status:

```
HTTP/1.1 101 Switching Protocols
Server: Ripple-0.26.0-rc1
Local-Address: 192.168.0.101:8421
Remote-Address: 63.104.209.13:8421
Upgrade: Ripple/1.2
Connection: Upgrade
Connect-As: Leaf
Transfer-Encoding: snappy
Protocol-Public-Key: aBRoQibi2jpDofohooFuzZi9nEzKw9Zdfc4ExVNmuXHaJpSPh8uJ
Protocol-Session-Cookie: 71ED064155FFADFA38782C5E0158CB26
```

### Fields

* *URL*

    The URL in the request line must be a single forward slash character
    ("/"). Requests with any other URL must be rejected. Different URL strings
    are reserved for future protocol implementations.

* *HTTP Version*

    The minimum required HTTP version is 1.1. Requests for HTTP versions
    earlier than 1.1 must be rejected.

* `User-Agent`

    Contains information about the software originating the request.
    The specification is identical to RFC2616 Section 14.43.

* `Server`

    Contains information about the software providing the response. The
    specification is identical to RFC2616 Section 14.38.

* `Local-Address`

    This field must be present and contain the string representation of the
    IP and port address of the local end of the connection as seen by the peer.

* `Remote-Address`

    This field must be present and contain the string representation of the
    IP and port address of the remote end of the connection as seen by the peer.

* `Upgrade`

    This field must be present and for requests consist of a comma delimited
    list of at least one element where each element is of the form "Ripple/"
    followed by the dotted major and minor protocol version number. For
    responses the value must be a single element matching one of the elements
    provided in the corresponding request field. If the server does not
    understand any of the requested protocols, 

* `Connection`

    This field must be present, containing the value 'Upgrade'.

* `Connect-As`

    For requests the value consists of a comma delimited list of elements
    where each element describes a possible connection type. Current connection
    types are:

    - leaf
    - peer

    If this field is omitted or the value is the empty string, then 'leaf' is
    assumed.

    For responses, the value must consist of exactly one element from the list
    of elements specified in the request. If a server does not recognize any
    of the connection types it must return a HTTP error response.

* `Protocol-Public-Key`

    This field value must be present, and contain a Base58 encoded value used
    as a server public key identifier.

* `Protocol-Session-Proof`

    This field must be present (TODO)

* _User Defined_

    The rippled operator may specify additional, optional fields and values
    through the configuration. These headers will be transmitted in the
    corresponding request or response messages.


---

[overlay_network]: http://en.wikipedia.org/wiki/Overlay_network
[protocol_buffers]: https://developers.google.com/protocol-buffers/
[upgrade_header]: http://en.wikipedia.org/wiki/HTTP/1.1_Upgrade_header
