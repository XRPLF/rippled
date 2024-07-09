# Resource::Manager #

The ResourceManager module has these responsibilities:

- Uniquely identify endpoints which impose load.
- Track the load used across endpoints.
- Provide an interface to share load information in a cluster.
- Warn and/or disconnect endpoints for imposing load.

## Description ##

To prevent monopolization of server resources or attacks on servers,
resource consumption is monitored at each endpoint. When consumption
exceeds certain thresholds, costs are imposed. Costs could include charging
additional XRP for transactions, requiring a proof of work to be
performed, or simply disconnecting the endpoint.

Currently, consumption endpoints include websocket connections used to
service clients, and peer connections used to create the peer to peer
overlay network implementing the Ripple protocol.

The current "balance" of a Consumer represents resource consumption
debt or credit. Debt is accrued when bad loads are imposed. Credit is
granted when good loads are imposed. When the balance crosses heuristic
thresholds, costs are increased on the endpoint. The balance is
represented as a unitless relative quantity. This balance is currently
held by the Entry struct in the impl/Entry.h file.

Costs associated with specific transactions are defined in the
impl/Fees files.

Although RPC connections consume resources, they are transient and
cannot be rate limited. It is advised not to expose RPC interfaces
to the general public.

## Consumer Types ##

Consumers are placed into three classifications (as identified by the
Resource::Kind enumeration):

 - InBound,
 - OutBound, and
 - Admin

 Each caller determines for itself the classification of the Consumer it is
 creating.

## Resource Loading ##

It is expected that a client will impose a higher load on the server
when it first connects: the client may need to catch up on transactions
it has missed, or get trust lines, or transfer fees.  The Manager must
expect this initial peak load, but not allow that high load to continue
because over the long term that would unduly stress the server.

If a client places a sustained high load on the server, that client
is initially given a warning message.  If that high load continues
the Manager may tell the heavily loaded server to drop the connection
entirely and not allow re-connection for some amount of time.

Each load is monitored by capturing peaks and then decaying those peak
values over time: this is implemented by the DecayingSample class.

## Gossip ##

Each server in a cluster creates a list of IP addresses of end points
that are imposing a significant load.  This list is called Gossip, which
is passed to other nodes in that cluster.  Gossip helps individual
servers in the cluster identify IP addreses that might be unduly loading
the entire cluster.  Again the recourse of the individual servers is to
drop connections to those IP addresses that occur commonly in the gossip.

## Access ##

In rippled, the Application holds a unique instance of Resource::Manager,
which may be retrieved by calling the method
`Application::getResourceManager()`.
