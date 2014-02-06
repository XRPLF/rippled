## Bootstrap Strategy

Fresh endpoints are ones we have seen recently via mtENDPOINTS.
These are best to give out to someone who needs additional
connections as quickly as possible, since it is very likely
that the fresh endpoints have open incoming slots.

Reliable endpoints are ones which are highly likely to be
connectible over long periods of time. They might not necessarily
have an incoming slot, but they are good for bootstrapping when
there are no peers yet. Typically these are what we would want
to store in a database or local config file for a future launch.

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
