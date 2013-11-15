# PeerFinder

The PeerFinder module has these responsibilities:

- Maintain a set of addresses suitable for bootstrapping into the overlay.
- Send and receive protocol messages for peer address discovery.
- Provide network addresses to other peers that need them.
- Maintain connections to the configured set of fixed peers.
- Track and manage peer connection slots.

## Description

## Terms

<table>
<tr>
  <td>Bootstrap</td>
  <td>The process by which a Ripple peer obtains the initial set of
      connections into the Ripple payment network overlay.
  </td></tr>
</tr>
<tr>
  <td>Overlay</td>
  <td>The connected graph of Ripple peers, overlaid on the public Internet.
  </td>
</tr>
<tr>
  <td>Peer</td>
  <td>A network server running the **rippled** daemon.
  </td>
</tr>
</table>

### Exposition

(Formerly in Manager.cpp, needs to be reformatted and tidied)

PeerFinder
----------

    Implements the logic for announcing and discovering IP addresses for
    for connecting into the Ripple network.

Introduction
------------

Each Peer (a computer running rippled) on the Ripple network requires a certain
number of connections to other peers. These connections form an "overlay
network." When a new peer wants to join the network, they need a robust source
of network addresses (IP addresses) in order to establish outgoing connections.
Once they have joined the network, they need a method of announcing their
availaibility of accepting incoming connections.

The Ripple network, like all peer to peer networks, defines a "directed graph"
where each node represents a computer running the rippled software, and each
vertex indicates a network connection. The direction of the connection tells
us whether it is an outbound or inbound connection (from the perspective of
a particular node).

Fact #1:
    The total inbound and outbound connections of any overlay must be equal.

This follows that for each node that has an established outbound connection,
there must exist another node that has received the corresponding inbound
connection.

When a new peer joins the network it may or may not wish to receive inbound
connections. Some peers are unable to accept incoming connections for various.
For security reasons they may be behind a firewall that blocks accept requests.
The administers may decide they don't want the connection traffic. Or they
may wish to connect only to specific peers. Or they may simply be misconfigured.

If a peer decides that it wishes to receive incoming connections, it needs
a method to announce its IP address and port number, the features that it
offers (for example, that it also services client requests), and the number
of available connection slots. This is to handle the case where the peer
reaches its desired number of peer connections, but may still want to inform
the network that it will service clients. It may also be desired to indicate
the number of free client slots.

Once a peer is connected to the network we need a way both to inform our
neighbors of our status with respect to accepting connections, and also to
learn about new fresh addresses to connect to. For this we will define the
mtENDPOINTS message.

"Bootstrap Strategy"
--------------------

Nouns:

    bootstrap_ip
        numeric IPAddress
    
    bootstrap_domain
        domain name / port combinations, resolution only

    bootstrap_url
        URL leading to a text file, with a series of entries.

    ripple.txt
        Separately parsed entity outside of PeerFinder that can provide
        bootstrap_ip, bootstrap_domain, and bootstrap_url items.

The process of obtaining the initial peer connections for accessing the Ripple
peer to peer network, when there are no current connections, is called
"bootstrapping." The algorithm is as follows:

1. If (    unusedLiveEndpoints.count() > 0
        OR activeConnectionAttempts.count() > 0)
        Try addresses from unusedLiveEndpoints
        return;
2. If (    domainNames.count() > 0 AND (
               unusedBootstrapIPs.count() == 0
            OR activeNameResolutions.count() > 0) )
        ForOneOrMore (DomainName that hasn't been resolved recently)
            Contact DomainName and add entries to the unusedBootstrapIPs
        return;
3. If (unusedBootstrapIPs.count() > 0)
        Try addresses from unusedBootstrapIPs
        return;
4. Try entries from [ips]
5. Try entries from [ips_urls]
6. Increment generation number and go to 1

    - Keep a map of all current outgoing connection attempts

"Connection Strategy"
---------------------

This is the overall strategy a peer uses to maintain its position in the Ripple
network graph

We define these values:

    peerCount (calculated)
        The number of currently connected and established peers

    outCount (calculated)
        The number of peers in PeerCount that are outbound connections.

    MinOutCount (hard-coded constant)
        The minimum number of OutCount we want. This also puts a floor
        on PeerCount. This protects against sybil attacks and makes
        sure that ledgers can get retrieved reliably.
        10 is the proposed value.

    MaxPeerCount (a constant set in the rippled.cfg)
        The maximum number of peer connections, inbound or outbound,
        that a peer wishes to maintain. Setting MaxPeerCount equal to
        or below MinOutCount would disallow incoming connections.

    OutPercent (a baked-in program constant for now)
        The peer's target value for OutCount. When the value of OutCount
        is below this number, the peer will employ the Outgoing Strategy
        to raise its value of OutCount. This value is initially a constant
        in the program, defined by the developers. However, it
        may be changed through the consensus process.
        15% is a proposed value.

However, lets consider the case where OutDesired is exactly equal to MaxPeerCount / 2.
In this case, a stable state will be reached when every peer is full, and
has exactly the same number of inbound and outbound connections. The problem
here is that there are now no available incoming connection slots. No new
peers can enter the network.

Lets consider the case where OutDesired is exactly equal to (MaxPeerCount / 2) - 1.
The stable state for this network (assuming all peers can accept incoming) will
leave us with network degree equal to MaxPeerCount - 2, with all peers having two
available incoming connection slots. The global number of incoming connection slots
will be equal to twice the number of nodes on the network. While this might seem to
be a desirable outcome, note that the connectedness (degree of the overlay) plays
a large part in determining the levels of traffic and ability to receive validations
from desired nodes. Having every node with available incoming connections also
means that entries in pong caches will continually fall out with new values and
information will become less useful.

For this reason, we advise that the value of OutDesired be fractional. Upon startup,
a node will use its node ID (its 160 bit unique ID) to decide whether to round the
value of OutDesired up or down. Using this method, we can precisely control the
global number of available incoming connection slots.

"Outgoing Strategy"
-------------------

This is the method a peer uses to establish outgoing connections into the
Ripple network.

A peer whose PeerCount is zero will use these steps:
    1. Attempt addresses from a local database of addresses
    2. Attempt addresses from a set of "well known" domains in rippled.cfg


This is the method used by a peer that is already connected to the Ripple network,
to adjust the number of outgoing connections it is maintaining.


"Incoming Strategy"
------------------------------

This is the method used by a peer to announce its ability and desire to receive
incoming connections both for the purpose of obtaining additional peer connections
and also for receiving requests from clients.

Overlay Network
    http://en.wikipedia.org/wiki/Overlay_network

Directed Graph
    http://en.wikipedia.org/wiki/Directed_graph

References:

Gnutella 0.6 Protocol
    2.2.2   Ping (0x00)
    2.2.3   Pong (0x01)
    2.2.4   Use of Ping and Pong messages
    2.2.4.1   A simple pong caching scheme
    2.2.4.2   Other pong caching schemes
    http://rfc-gnutella.sourceforge.net/src/rfc-0_6-draft.html

Revised Gnutella Ping Pong Scheme
    By Christopher Rohrs and Vincent Falco
    http://rfc-gnutella.sourceforge.net/src/pong-caching.html

