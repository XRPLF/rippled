# ResourceManager

The ResourceManager module has these responsibilities:

- Uniquely identify endpoints which impose load.
- Track the load used across endpoints.
- Provide an interface to share load information in a cluster.
- Warn and/or disconnect endpoints for imposing load.

## Description

To prevent monopolization of server resources or attacks on servers,
resource consumption is monitored at each endpoint. When consumption
exceeds certain thresholds, costs are imposed. Costs include charging
additional XRP for transactions, requiring a proof of work to be
performed, or simply disconnecting the endpoint.

Currently, consumption endpoints include websocket connections used to
service clients, and peer connections used to create the peer to peer
overlay network implementing the Ripple protcool.

The current "balance" of a Consumer represents resource consumption
debt or credit. Debt is accrued when bad loads are imposed. Credit is
granted when good loads are imposed. When the balance crosses heuristic
thresholds, costs are increased on the endpoint. The balance is
represented as a unitless relative quantity.

Although RPC connections consume resources, they are transient and
cannot be rate limited. It is advised not to expose RPC interfaces
to the general public.
