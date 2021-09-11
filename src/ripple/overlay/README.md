# Overlay

## Introduction

The _XRP Ledger network_ consists of a collection of _peers_ running
**`rippled`** or other compatible software. Each peer maintains multiple
outgoing connections and optional incoming connections to other peers.
These connections are made over both the public Internet and private local
area networks. This network defines a connected directed graph of nodes
where vertices are instances of `rippled` and edges are persistent TCP/IP
connections. Peers send and receive messages to other connected peers. This
peer to peer network, layered on top of the public and private Internet,
forms an [_overlay network_][overlay_network]. The contents of the messages
and the behavior of peers in response to the messages, plus the information
exchanged during the handshaking phase of connection establishment, defines
the _XRP Ledger peer protocol_ (or _protocol_ in this context).

## Overview

Each connection is represented by a _Peer_ object. The Overlay manager
establishes, receives, and maintains connections to peers. Protocol
messages are exchanged between peers and serialized using
[_Google Protocol Buffers_][protocol_buffers].

### Structure

Each connection between peers is identified by its connection type, which
affects the behavior of message routing. At present, only a single connection
type is supported: **Peer**.

## Handshake

To establish a protocol connection, a peer makes an outgoing TLS encrypted
connection to a remote peer, then sends an HTTP request with no message body.

### HTTP

The HTTP [request](https://www.w3.org/Protocols/rfc2616/rfc2616-sec5.html) must:

- Use HTTP version 1.1.
- Specify a request URI consisting of a single forward slash character ("/")
indicating the server root. Requests using different URIs are reserved for
future protocol implementations.
- Use the [_HTTP/1.1 Upgrade_][upgrade_header] mechanism with additional custom
fields to communicate protocol specific information related to the upgrade.

HTTP requests which do not conform to this requirements must generate an
appropriate HTTP error and result in the connection being closed.

Upon receipt of a well-formed HTTP upgrade request, and validation of the
protocol specific parameters, a peer will either send back a HTTP 101 response
and switch to the requested protocol, or a message indicating that the request
failed (e.g. by sending HTTP 400 "Bad Request" or HTTP 503 "Service Unavailable").

##### Example HTTP Upgrade Request

```
GET / HTTP/1.1
User-Agent: rippled-1.4.0-b1+DEBUG
Upgrade: RTXP/1.2, XRPL/2.0
Connection: Upgrade
Connect-As: Peer
Crawl: public
Network-ID: 1
Network-Time: 619234489
Public-Key: n94MvLTiHQJjByfGZzvQewTxQP2qjF6shQcuHwCjh5WoiozBrdpX
Session-Signature: MEUCIQCOO8tHOh/tgCSRNe6WwOwmIF6urZ5uSB8l9aAf5q7iRAIgA4aONKBZhpP5RuOuhJP2dP+2UIRioEJcfU4/m4gZdYo=
Session-EKM-Signature: 4F73F4DF23453250BDDF81AFBB5CBF0FD32B92AAF548331DB0ECF1DB988BE2F3D7264621FC1E93404B77612F235AC6FFD4F1B7B4D420E8071384273D9A60201D
Remote-IP: 192.0.2.79
Closed-Ledger: 4F73F45E95343F7446F91B5F70E3AE4E155CCAA2B1CF2F267C99FD39C1C24178
Previous-Ledger: F9D75AAB7D975BADC6D008C4AE94D9B7B6AA323EEE4B133F5B75CE92445C88ED
```

##### Example HTTP Upgrade Response (Success)


```
HTTP/1.1 101 Switching Protocols
Connection: Upgrade
Upgrade: RTXP/1.2
Connect-As: Peer
Server: rippled-1.3.1
Crawl: public
Public-Key: n9K1ZXXXzzA3dtgKBuQUnZXkhygMRgZbSo3diFNPVHLMsUG5osJM
Session-Signature: MEQCIHMlLGTcGyPvHji7WY2nRM2B0iSBnw9xeDUGW7bPq7IjAiAmy+ofEu+8nOq2eChRTr3wjoKi3EYRqLgzP+q+ORFcig==
Network-Time: 619234797
Closed-Ledger: h7HL85W9ywkex+G7p42USVeV5kE04CWK+4DVI19Of8I=
Previous-Ledger: EPvIpAD2iavGFyyZYi8REexAXyKGXsi1jMF7OIBY6/Y=
```

##### Example HTTP Upgrade Response (Failure: no slots available)

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

#### Standard Fields

| Field Name          	|  Request          	| Response          	|
|---------------------	|:-----------------:	|:-----------------:	|
| `User-Agent`        	| :heavy_check_mark: 	|                   	|

The `User-Agent` field indicates the version of the software that the
peer that is making the HTTP request is using. No semantic meaning is
assigned to the value in this field but it is recommended that implementations
specify the version of the software that is used.

See [RFC2616 &sect;14.43](https://www.w3.org/Protocols/rfc2616/rfc2616-sec14.html#sec14.43).

| Field Name          	|  Request          	| Response          	|
|---------------------	|:-----------------:	|:-----------------:	|
| `Server`            	|                   	| :heavy_check_mark: 	|

The `Server` field indicates the version of the software that the
peer that is processing the HTTP request is using. No semantic meaning is
assigned to the value in this field but it is recommended that implementations
specify the version of the software that is used.

See [RFC2616 &sect;14.38](https://www.w3.org/Protocols/rfc2616/rfc2616-sec14.html#sec14.38).

| Field Name          	|  Request          	| Response          	|
|---------------------	|:-----------------:	|:-----------------:	|
| `Connection`        	| :heavy_check_mark: 	| :heavy_check_mark: 	|

The `Connection` field should have a value of `Upgrade` to indicate that a
request to upgrade the connection is being performed.

See [RFC2616 &sect;14.10](https://www.w3.org/Protocols/rfc2616/rfc2616-sec14.html#sec14.10).

| Field Name          	|  Request          	| Response          	|
|---------------------	|:-----------------:	|:-----------------:	|
| `Upgrade`           	| :heavy_check_mark: 	| :heavy_check_mark: 	|

The `Upgrade` field is part of the standard connection upgrade mechanism and
must be present in both requests and responses. It is used to negotiate the
version of the protocol that will be used after the upgrade request completes.

For requests, it should consist of a comma delimited list of at least one
element, where each element specifies a protocol version that the requesting
server is willing to use.

For responses, it should a consist of _single element_ matching one of the
elements provided in the corresponding request. If the server does not understand
any of the available protocol versions, the upgrade request should fail with an
appropriate HTTP error code (e.g. by sending an HTTP 400 "Bad Request" response).

Protocol versions are string of the form `XRPL/` followed by a dotted major
and minor protocol version number, where the major number is greater than or
equal to 2 and the minor is greater than or equal to 0.

See [RFC 2616 &sect;14.42](https://www.w3.org/Protocols/rfc2616/rfc2616-sec14.html#sec14.42)


#### Custom Fields

| Field Name          	|  Request          	| Response          	|
|---------------------	|:-----------------:	|:-----------------:	|
| `Connect-As`        	| :heavy_check_mark: 	| :heavy_check_mark: 	|

The mandatory `Connect-As` field is used to specify that type of connection
that is being requested.

For requests the value consists of a comma delimited list of elements, where
each element describes a possible connection type. Only one connection types
is supported at present: **`peer`**.

For responses, the value must consist of exactly one element from the list of
elements specified in the request. If a server processing a request does not
recognize any of the connection types, the request should fail with an
appropriate HTTP error code (e.g. by sending an HTTP 400 "Bad Request" response).


| Field Name          	|  Request          	| Response          	|
|---------------------	|:-----------------:	|:-----------------:	|
| `Remote-IP`         	| :white_check_mark: 	| :white_check_mark: 	|

The optional `Remote-IP` field contains the string representation of the IP
address of the remote end of the connection as seen from the peer that is
sending the field.

By observing values of this field from a sufficient number of different
servers, a peer making outgoing connections can deduce its own IP address.


| Field Name          	|  Request          	| Response          	|
|---------------------	|:-----------------:	|:-----------------:	|
| `Local-IP`          	| :white_check_mark: 	| :white_check_mark: 	|

The optional `Local-IP` field contains the string representation of the IP
address that the peer sending the field believes to be its own.

Servers receiving this field can detect IP address mismatches, which may
indicate a potential man-in-the-middle attack.


| Field Name          	|  Request          	| Response          	|
|---------------------	|:-----------------:	|:-----------------:	|
| `Network-ID`        	| :white_check_mark: 	| :white_check_mark: 	|

The optional `Network-ID` can be used to identify to which of several
[parallel networks](https://xrpl.org/parallel-networks.html) the server
sending the field is joined.

The value, if the field is present, is a 32-bit unsigned integer. The
following well-known values are in use:

- **0**: The "main net"
- **1**: The Ripple-operated [Test Net](https://xrpl.org/xrp-test-net-faucet.html).

If a server configured to join one network receives a connection request from a
server configured to join another network, the request should fail with an
appropriate HTTP error code (e.g. by sending an HTTP 400 "Bad Request" response).


| Field Name          	|  Request          	| Response          	|
|---------------------	|:-----------------:	|:-----------------:	|
| `Network-Time`      	| :white_check_mark: 	| :white_check_mark: 	|

The optional `Network-Time` field reports the current [time](https://xrpl.org/basic-data-types.html#specifying-time)
according to sender's internal clock.

Servers should fail a connection if their clocks are not within 20 seconds of
each other with an appropriate HTTP error code (e.g. by sending an HTTP 400
"Bad Request" response).

It is highly recommended that servers synchronize their clocks using time
synchronization software. For more on this topic, please visit [ntp.org](http://www.ntp.org/).


| Field Name          	|  Request          	| Response          	|
|---------------------	|:-----------------:	|:-----------------:	|
| `Public-Key`        	| :heavy_check_mark: 	| :heavy_check_mark: 	|

The mandatory `Public-Key` field identifies the sending server's public key,
encoded in base58 using the standard encoding for node public keys.

See: https://xrpl.org/base58-encodings.html


| Field Name          	|  Request          	| Response          	|
|---------------------	|:-----------------:	|:-----------------:	|
| `Server-Domain`   	| :white_check_mark: 	| :white_check_mark: 	|

The optional `Server-Domain` field allows a server to report the domain that
it is operating under. The value is configured by the server administrator in
the configuration file using the `[server_domain]` key.

The value is advisory and is not used by the code at this time, except for
reporting purposes. External tools should verify this value prior to using
it by attempting to locate a [TOML file](https://xrpl.org/xrp-ledger-toml.html)
under the specified domain and locating the public key of this server under the
`[NODES]` key.

Sending a malformed domain will prevent a connection from being established.


| Field Name              |  Request          	| Response          	|
|-------------------------|:-----------------:	|:-----------------:	|
| `Session-EKM-Signature` | :heavy_check_mark: 	| :heavy_check_mark: 	|

The `Session-EKM-Signature` field supersedes the `Session-Signature` field and is
mandatory if `Session-Signature` is not present. It is used to secure the peer
link against certain types of attack. For more details see the section titled
"Session Security" below.

The value is specified in **HEX** encoding.


| Field Name          	|  Request          	| Response          	|
|---------------------	|:-----------------:	|:-----------------:	|
| `Session-Signature` 	| :heavy_check_mark: 	| :heavy_check_mark: 	|

The `Session-Signature` field is a legacy field that has been superseded by the
`Session-EKM-Signature` field. It will be removed in a future release of the
software.

It is used to secure the peer link against certain types of attack. For more
details see the section titled "Session Signature" below.

The value is presently encoded using **Base64** encoding, but implementations
should support both **Base64** and **HEX** encoding for this value.


| Field Name          	|  Request          	| Response          	|
|---------------------	|:-----------------:	|:-----------------:	|
| `Crawl`             	| :white_check_mark: 	| :white_check_mark: 	|

The optional `Crawl` field can be used by a server to indicate whether peers
should include it in crawl reports.

The field can take two values:
- **`Public`**: The server's IP address and port should be included in crawl
reports.
- **`Private`**: The server's IP address and port should not be included in
crawl reports. _This is the default, if the field is omitted._

For more on the Peer Crawler, please visit https://xrpl.org/peer-crawler.html.


| Field Name          	|  Request          	| Response          	|
|---------------------	|:-----------------:	|:-----------------:	|
| `Closed-Ledger`     	| :white_check_mark: 	| :white_check_mark: 	|

If present, identifies the hash of the last ledger that the sending server
considers to be closed.

The field data should be encoded using **HEX**, but implementations should
correctly interpret both **Base64** and **HEX** encodings.
    
| Field Name          	|  Request          	| Response          	|
|---------------------	|:-----------------:	|:-----------------:	|
| `Previous-Ledger`   	| :white_check_mark: 	| :white_check_mark: 	|

If present, identifies the hash of the parent ledger that the sending server
considers to be closed.

The field data should be encoded using **HEX**, but implementations should
correctly interpret both **Base64** and **HEX** encodings.

#### Additional Headers

An implementation or operator may specify additional, optional fields
and values in both requests and responses.

Implementations should not reject requests because of the presence of fields
that they do not understand.


### Session Security

Even for SSL/TLS encrypted connections, it is possible for an attacker to mount
relatively inexpensive MITM attacks that can be extremely hard to detect and
may afford the attacker the ability to intelligently tamper with messages
exchanged between the two endpoints.

This risk can be mitigated if at least one side has a certificate from a CA that
is trusted by the other endpoint, but having a certificate is not always
possible (or, indeed, desirable) in a decentralized and permissionless network.

Ultimately, the goal is to ensure that two endpoints A and B know that they are
talking directly to each other over a single end-to-end SSL/TLS session instead
of two separate SSL/TLS sessions, with an attacker acting as a proxy.

The XRP Ledger protocol prevents this attack by leveraging the fact that the two
servers each have a node identity, in the form of **`secp256k1`** keypairs, and
use that, along with a secure fingerprint associated with the SSL/TLS session to
strongly bind the SSL/TLS session to the node identities of each of the servers
at the end of the SSL/TLS session.

The fingerprint is never shared over the wire (the two endpoints calculate it
independently) and is signed by each server using its public **`secp256k1`**
node identity. The resulting signature is transferred over the SSL/TLS session
during the protocol handshake phase.

Each side of the link will verify that the provided signature is from the claimed
public key against the session's unique fingerprint. If this signature check fails
then the link **MUST** be dropped.

If an attacker, Eve, establishes two separate SSL sessions with Alice and Bob, the
fingerprints of the two sessions will be different, and Eve will not be able to
sign the fingerprint of her session with Bob with Alice's private key, or the
fingerprint of her session with Alice with Bob's private key, and so both A and
B will know that an active MITM attack is in progress and will close their
connections.

If Eve simply proxies the raw bytes, she will be unable to decrypt the data being
transferred between A and B and will not be able to intelligently tamper with the
message stream between Alice and Bob, although she may be still be able to inject
delays or terminate the link.


# Clustering #

A cluster consists of several servers, typically under common administration,
that are configured to work cooperatively by sharing server load information,
details about shards, optimizing processing to avoid duplicating work that other
cluster members have done, and more.

## Configuration ##

A server's public key can be determined from the output of the `server_info`
command.  The key is in the `pubkey_node` value, and is a text string
beginning with the letter `n`.  The key is maintained across runs in a
database.

Cluster members are configured in the `rippled.cfg` file under
`[cluster_nodes]`.  Each member should be configured on a separate line,
beginning with its node public key, followed optionally by a space and a
friendly name.

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

Several "local" checks are also bypassed. For example, a server will not reject
a transaction from a cluster peer because the fee does not meet its current
relay fee. It is preferable to keep the cluster in agreement and permit
confirmation from one cluster member to more reliably indicate the transaction's
acceptance by the cluster.

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
