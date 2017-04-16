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
