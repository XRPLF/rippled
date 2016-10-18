
# PeerFinder

## Introduction

The _Ripple payment network_ consists of a collection of _peers_ running the
**rippled software**. Each peer maintains multiple outgoing connections and
optional incoming connections to other peers. These connections are made over
both the public Internet and private local area networks. This network defines
a fully connected directed graph of nodes. Peers send and receive messages to
other connected peers. This peer to peer network, layered on top of the public
and private Internet, forms an [_overlay network_][overlay_network].

## Bootstrapping

When a peer comes online it needs a set of IP addresses to connect to in order to
gain initial entry into the overlay in a process called _bootstrapping_. Once they
have established an initial set of these outbound peer connections, they need to
gain additional addresses to establish more outbound peer connections until the
desired limit is reached. Furthermore, they need a mechanism to advertise their
IP address to new or existing peers in the overlay so they may receive inbound
connections up to some desired limit. And finally, they need a mechanism to provide
inbound connection requests with an alternate set of IP addresses to try when they
have already reached their desired maximum number of inbound connections.

PeerFinder is a self contained module that provides these services, along with some
additional overlay network management services such as _fixed slots_ and _cluster
slots_.

## Features

PeerFinder has these responsibilities

* Maintain a persistent set of endpoint addresses suitable for bootstrapping
  into the peer to peer overlay, ranked by relative locally observed utility.

* Send and receive protocol messages for discovery of endpoint addresses.

* Provide endpoint addresses to new peers that need them.

* Maintain connections to a configured set of fixed peers.

* Impose limits on the various slots consumed by peer connections.

* Initiate outgoing connection attempts to endpoint addresses to maintain the
  overlay connectivity and fixed peer policies.

* Verify the connectivity of neighbors who advertise inbound connection slots.

* Prevent duplicate connections and connections to self.

---

# Concepts

## Manager

The `Manager` is an application singleton which provides the primary interface
to interaction with the PeerFinder.

### Autoconnect

The Autoconnect feature of PeerFinder automatically establishes outgoing
connections using addresses learned from various sources including the
configuration file, the result of domain name lookups, and messages received
from the overlay itself.

### Callback

PeerFinder is an isolated code module with few external dependencies. To perform
socket specific activities such as establishing outgoing connections or sending
messages to connected peers, the Manager is constructed with an abstract
interface called the `Callback`. An instance of this interface performs the
actual required operations, making PeerFinder independent of the calling code.

### Config

The `Config` structure defines the operational parameters of the PeerFinder.
Some values come from the configuration file while others are calculated via
tuned heuristics. The fields are as follows:

* `autoConnect`
 
  A flag indicating whether or not the Autoconnect feature is enabled.

* `wantIncoming`

  A flag indicating whether or not the peer desires inbound connections. When
  this flag is turned off, a peer will not advertise itself in Endpoint
  messages.

* `listeningPort`

  The port number to use when creating the listening socket for peer
  connections.

* `maxPeers`

  The largest number of active peer connections to allow. This includes inbound
  and outbound connections, but excludes fixed and cluster peers. There is an
  implementation defined floor on this value.

* `outPeers`

  The number of automatic outbound connections that PeerFinder will maintain
  when the Autoconnect feature is enabled. The value is computed with fractional
  precision as an implementation defined percentage of `maxPeers` subject to
  an implementation defined floor. An instance of the PeerFinder rounds the
  fractional part up or down using a uniform random number generated at
  program startup. This allows the outdegree of the overlay network to be
  controlled with fractional precision, ensuring that all inbound network
  connection slots are not consumed (which would make it difficult for new
  participants to enter the network).

Here's an example of how the network might be structured with a fractional
value for outPeers:

**(Need example here)**

### Livecache

The Livecache holds relayed IP addresses that have been received recently in
the form of Endpoint messages via the peer to peer overlay. A peer periodically
broadcasts the Endpoint message to its neighbors when it has open inbound
connection slots. Peers store these messages in the Livecache and periodically
forward their neighbors a handful of random entries from their Livecache, with
an incremented hop count for each forwarded entry.

The algorithm for sending a neighbor a set of Endpoint messages chooses evenly
from all available hop counts on each send. This ensures that each peer
will see some entries with the farthest hops at each iteration. The result is
to expand a peer's horizon with respect to which overlay endpoints are visible.
This is designed to force the overlay to become highly connected and reduce
the network diameter with each connection establishment.

When a peer receives an Endpoint message that originates from a neighbor
(identified by a hop count of zero) for the first time, it performs an incoming
connection test on that neighbor by initiating an outgoing connection to the
remote IP address as seen on the connection combined with the port advertised
in the Endpoint message. If the test fails, then the peer considers its neighbor
firewalled (intentionally or due to misconfiguration) and no longer forwards
Endpoint messages for that peer. This prevents poor quality unconnectible
addresses from landing in the caches. If the incoming connection test passes,
then the peer fills in the Endpoint message with the remote address as seen on
the connection before storing it in its cache and forwarding it to other peers.
This relieves the neighbor from the responsibility of knowing its own IP address
before it can start receiving incoming connections.

Livecache entries expire quickly. Since a peer stops advertising itself when
it no longer has available inbound slots, its address will shortly after stop
being handed out by other peers. Livecache entries are very likely to result
in both a successful connection establishment and the acquisition of an active
outbound slot. Compare this with Bootcache addresses, which are very likely to
be connectible but unlikely to have an open slot.

Because entries in the Livecache are ephemeral, they are not persisted across
launches in the database. The Livecache is continually updated and expired as
Endpoint messages are received from the overlay over time.

### Bootcache

The `Bootcache` stores IP addresses useful for gaining initial connections.
Each address is associated with the following metadata:
 
* **Valence**

  A signed integer which represents the number of successful
  consecutive connection attempts when positive, and the number of
  failed consecutive connection attempts when negative. If an outgoing
  connection attempt to the corresponding IP address fails to complete the
  handshake the valence is reset to negative one. This harsh penalty is
  intended to prevent popular servers from forever remaining top ranked in
  all peer databases.

When choosing addresses from the boot cache for the purpose of
establishing outgoing connections, addresses are ranked in decreasing order of
valence. The Bootcache is persistent. Entries are periodically inserted and
updated in the corresponding SQLite database during program operation. When
**rippled** is launched, the existing Bootcache database data is accessed and
loaded to accelerate the bootstrap process.

Desirable entries in the Bootcache are addresses for servers which are known to
have high uptimes, and for which connection attempts usually succeed. However,
these servers do not necessarily have available inbound connection slots.
However, it is assured that these servers will have a well populated Livecache
since they will have moved towards the core of the overlay over their high
uptime. When a connected server is full it will return a handful of new
addresses from its Livecache and gracefully close the connection. Addresses
from the Livecache are highly likely to have inbound connection slots and be
connectible.

For security, all information that contributes to the ranking of Bootcache
entries is observed locally. PeerFinder never trusts external sources of information.

### Slot

Each TCP/IP socket that can participate in the peer to peer overlay occupies
a slot. Slots have properties and state associated with them:

#### State (Slot)

The slot state represents the current stage of the connection as it passes
through the business logic for establishing peer connections.

* `accept`

  The accept state is an initial state resulting from accepting an incoming
  connection request on a listening socket. The remote IP address and port
  are known, and a handshake is expected next.

* `connect`

  The connect state is an initial state used when actively establishing outbound
  connection attempts. The desired remote IP address and port are known.

* `connected`

  When an outbound connection attempt succeeds, it moves to the connected state.
  The handshake is initiated but not completed.

* `active`

  The state becomes Active when a connection in either the Accepted or Connected
  state completes the handshake process, and a slot is available based on the
  properties. If no slot is available when the handshake completes, the socket
  is gracefully closed.

* `closing`

  The Closing state represents a connected socket in the process of being
  gracefully closed.

#### Properties (Slot)

Slot properties may be combined and are not mutually exclusive.

* **Inbound**

  An inbound slot is the condition of a socket which has accepted an incoming
  connection request. A connection which is not inbound is by definition
  outbound.

* **Fixed**

  A fixed slot is a desired connection to a known peer identified by IP address,
  usually entered manually in the configuration file. For the purpose of
  establishing outbound connections, the peer also has an associated port number
  although only the IP address is checked to determine if the fixed peer is
  already connected. Fixed slots do not count towards connection limits.

* **Cluster**

  A cluster slot is a connection which has completed the handshake stage, whose
  public key matches a known public key usually entered manually in the
  configuration file or learned through overlay messages from other trusted
  peers. Cluster slots do not count towards connection limits.

* **Superpeer** (forthcoming)

  A superpeer slot is a connection to a peer which can accept incoming
  connections, meets certain resource availaibility requirements (such as
  bandwidth, CPU, and storage capacity), and operates full duplex in the
  overlay. Connections which are not superpeers are by definition leaves. A
  leaf slot is a connection to a peer which does not route overlay messages to
  other peers, and operates in a partial half duplex fashion in the overlay.

#### Fixed Slots

Fixed slots are identified by IP address and set up during the initialization
of the Manager, usually from the configuration file. The Logic will always make
outgoing connection attempts to each fixed slot which is not currently
connected. If we receive an inbound connection from an endpoint whose address
portion (without port) matches a fixed slot address, we consider the fixed
slot to be connected.

#### Cluster Slots

Cluster slots are identified by the public key and set up during the
initialization of the manager or discovered upon receipt of messages in the
overlay from trusted connections.

--------------------------------------------------------------------------------

# Algorithms

## Connection Strategy

The _Connection Strategy_ applies the configuration settings to establish
desired outbound connections. It runs periodically and progresses through a
series of stages, remaining in each stage until a condition is met

### Stage 1: Fixed Slots

This stage is invoked when the number of active fixed connections is below the
number of fixed connections specified in the configuration, and one of the
following is true:

* There are eligible fixed addresses to try
* Any outbound connection attempts are in progress

Each fixed address is associated with a retry timer. On a fixed connection
failure, the timer is reset so that the address is not tried for some amount
of time, which increases according to a scheduled sequence up to some maximum
which is currently set to approximately one hour between retries. A fixed
address is considered eligible if we are not currently connected or attempting
the address, and its retry timer has expired.

The PeerFinder makes its best effort to become fully connected to the fixed
addresses specified in the configuration file before moving on to establish
outgoing connections to foreign peers. This security feature helps rippled
establish itself with a trusted set of peers first before accepting untrusted
data from the network.

### Stage 2: Livecache

The Livecache is invoked when Stage 1 is not active, autoconnect is enabled,
and the number of active outbound connections is below the number desired. The
stage remains active while:

* The Livecache has addresses to try
* Any outbound connection attempts are in progress

PeerFinder makes its best effort to exhaust addresses in the Livecache before
moving on to the Bootcache, because Livecache addresses are highly likely
to be connectible (since they are known to have been online within the last
minute), and highly likely to have an open slot for an incoming connection
(because peers only advertise themselves in the Livecache when they have
open slots).

### Stage 3: Bootcache

The Bootcache is invoked when Stage 1 and Stage 2 are not active, autoconnect
is enabled, and the number of active outbound connections is below the number
desired. The stage remains active while:

* There are addresses in the cache that have not been tried recently.

Entries in the Bootcache are ranked, with highly connectible addresses preferred
over others. Connection attempts to Bootcache addresses are very likely to
succeed but unlikely to produce an active connection since the peers likely do
not have open slots. Before the remote peer closes the connection it will send
a handful of addresses from its Livecache to help the new peer coming online
obtain connections.

--------------------------------------------------------------------------------

# References

Much of the work in PeerFinder was inspired by earlier work in Gnutella:

[Revised Gnutella Ping Pong Scheme](http://rfc-gnutella.sourceforge.net/src/pong-caching.html)<br>
_By Christopher Rohrs and Vincent Falco_

[Gnutella 0.6 Protocol:](http://rfc-gnutella.sourceforge.net/src/rfc-0_6-draft.html) Sections:
* 2.2.2   Ping (0x00)
* 2.2.3   Pong (0x01)
* 2.2.4   Use of Ping and Pong messages
* 2.2.4.1   A simple pong caching scheme
* 2.2.4.2   Other pong caching schemes

[overlay_network]: http://en.wikipedia.org/wiki/Overlay_network
