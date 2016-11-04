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
User-Agent: rippled-0.27.0
Local-Address: 192.168.0.101:8421
Upgrade: RTXP/1.2, RTXP/1.3
Connection: Upgrade
Connect-As: Leaf, Peer
Accept-Encoding: identity, zlib, snappy
Public-Key: aBRoQibi2jpDofohooFuzZi9nEzKw9Zdfc4ExVNmuXHaJpSPh8uJ
Session-Signature: 71ED064155FFADFA38782C5E0158CB26
```

Upon receipt of a well-formed HTTP request the remote peer will send back a
HTTP response indicating the connection status:

```
HTTP/1.1 101 Switching Protocols
Server: rippled-0.27.0
Remote-Address: 63.104.209.13
Upgrade: RTXP/1.2
Connection: Upgrade
Connect-As: Leaf
Transfer-Encoding: snappy
Public-Key: aBRoQibi2jpDofohooFuzZi9nEzKw9Zdfc4ExVNmuXHaJpSPh8uJ
Session-Signature: 71ED064155FFADFA38782C5E0158CB26
```

If the remote peer has no available slots, the HTTP status code 503 (Service
Unavailable) is returned, with an optional content body in JSON format that
may contain additional information such as IP and port addresses of other
servers that may have open slots:

```
HTTP/1.1 503 Service Unavailable
Server: rippled-0.27.0
Remote-Address: 63.104.209.13
Content-Length: 253
Content-Type: application/json
{"peer-ips":["54.68.219.39:51235","54.187.191.179:51235",
"107.150.55.21:6561","54.186.230.77:51235","54.187.110.243:51235",
"85.127.34.221:51235","50.43.33.236:51235","54.187.138.75:51235"]}
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

* `Remote-Address` (optional)

    This optional field contains the string representation of the IP
    address of the remote end of the connection as seen by the peer.
    By observing values of this field from a sufficient number of different
    servers, a peer making outgoing connections can deduce its own IP address.

* `Upgrade`

    This field must be present and for requests consist of a comma delimited
    list of at least one element where each element is of the form "RTXP/"
    followed by the dotted major and minor protocol version number. For
    responses the value must be a single element matching one of the elements
    provided in the corresponding request field. If the server does not
    understand any of the requested protocols, the request is rejected.

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

* `Public-Key`

    This field value must be present, and contain a base 64 encoded value used
    as a server public key identifier.

* `Session-Signature`

    This field must be present. It contains a cryptographic token formed
    from the SHA512 hash of the shared data exchanged during SSL handshaking.
    For more details see the corresponding source code.

* `Crawl` (optional)

    If present, and the value is "public" then neighbors will report the IP
    address to crawler requests. If absent, neighbor's default behavior is to
    not report IP addresses.

* _User Defined_ (Unimplemented)

    The rippled operator may specify additional, optional fields and values
    through the configuration. These headers will be transmitted in the
    corresponding request or response messages.

# Ripple Clustering #

A cluster consists of more than one Ripple server under common
administration that share load information, distribute cryptography
operations, and provide greater response consistency.

Cluster nodes are identified by their public node keys. Cluster nodes
exchange information about endpoints that are imposing load upon them.
Cluster nodes share information about their internal load status.  Cluster
nodes do not have to verify the cryptographic signatures on messages
received from other cluster nodes.

## Configuration ##

A server's public key can be determined from the output of the `server_info`
command.  The key is in the `pubkey_node` value, and is a text string
beginning with the letter `n`.  The key is maintained across runs in a
database.

Cluster members are configured in the `rippled.cfg` file under
`[cluster_nodes]`.  Each member should be configured on a line beginning
with the node public key, followed optionally by a space and a friendly
name.

Because cluster members can introduce other cluster members, it is not
necessary to configure every cluster member on every other cluster member.
If a hub and spoke system is used, it is sufficient to configure every
cluster member on the hub(s) and only configure the hubs on the spokes.
That is, each spoke does not need to be configured on every other spoke.

New spokes can be added as follows:

- In the new spoke's `[cluster_nodes]`, include each hub's public node key
- Start the spoke server and determine its public node key
- Configure each hub with the new spoke's public key
- Restart each hub, one by one
- Restart the spoke

## Transaction Behavior ##

When a transaction is received from a cluster member, several normal checks
are bypassed:

Signature checking is bypassed because we trust that a cluster member would
not relay a transaction with an incorrect signature.  Validators may wish to
disable this feature, preferring the additional load to get the additional
security of having validators check each transaction.

Local checks for transaction checking are also bypassed. For example, a
server will not reject a transaction from a cluster peer because the fee
does not meet its current relay fee.  It is preferable to keep the cluster
in agreement and permit confirmation from one cluster member to more
reliably indicate the transaction's acceptance by the cluster.

## Server Load Information ##

Cluster members exchange information on their server's load level. The load
level is essentially the amount by which the normal fee levels are multiplied
to get the server's fee for relaying transactions.

A server's effective load level, and the one it uses to determine its relay
fee, is the highest of its local load level, the network load level, and the
cluster load level. The cluster load level is the median load level reported
by a cluster member.

## Gossip ##

Gossip is the mechanism by which cluster members share information about
endpoints (typically IPv4 addresses) that are imposing unusually high load
on them.  The endpoint load manager takes into account gossip to reduce the
amount of load the endpoint is permitted to impose on the local server
before it is warned, disconnected, or banned.

Suppose, for example, that an attacker controls a large number of IP
addresses, and with these, he can send sufficient requests to overload a
server.  Without gossip, he could use these same addresses to overload all
the servers in a cluster.  With gossip, if he chooses to use the same IP
address to impose load on more than one server, he will find that the amount
of load he can impose before getting disconnected is much lower.

## Monitoring ##

The `peers` command will report on the status of the cluster. The `cluster`
object will contain one entry for each member of the cluster (either configured
or introduced by another cluster member). The `age` field is the number of
seconds since the server was last heard from. If the server is reporting an
elevated cluster fee, that will be reported as well.

In the `peers` object, cluster members will contain a `cluster` field set to `true`.

---

[overlay_network]: http://en.wikipedia.org/wiki/Overlay_network
[protocol_buffers]: https://developers.google.com/protocol-buffers/
[upgrade_header]: http://en.wikipedia.org/wiki/HTTP/1.1_Upgrade_header
