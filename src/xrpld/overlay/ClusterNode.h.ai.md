# ClusterNode.h

`ClusterNode` is a small, immutable-identity value type that represents a single trusted peer within an XRPL cluster. It lives in `src/xrpld/overlay/ClusterNode.h` and is the element type stored inside `Cluster`'s `std::set`. The class is intentionally minimal: it captures exactly the four pieces of information that cluster members exchange with one another — who you are, what you call yourself, how loaded you are, and when you last reported.

## Role in the Cluster Subsystem

An XRPL cluster is a set of validator nodes operated by the same entity that extend automatic trust to each other. Cluster membership is declared in the server configuration (the `[cluster_nodes]` section) as a list of base58-encoded node public keys with optional human-readable names. At runtime, cluster peers periodically broadcast `TMCluster` protocol messages containing each known member's current load fee and report timestamp. `ClusterNode` is the in-memory record that holds that gossip state for a single peer.

`Cluster` (defined in `Cluster.h`) owns a `std::set<ClusterNode, Comparator>` keyed on `PublicKey`. The `Comparator` struct uses transparent comparison (`is_transparent = std::true_type`) so the set can be searched with a raw `PublicKey` without constructing a dummy `ClusterNode` — enabling the erase/insert idiom in `Cluster::update()` to work cleanly alongside direct `find(PublicKey)` lookups.

## Fields and Their Purpose

`identity_` is declared `const` — it is the primary key and must never change after construction. This is why `Cluster::update()` cannot mutate a `ClusterNode` in place: `std::set` elements are logically immutable (you cannot modify a key once inserted without invalidating the container's order). Instead, `update()` erases the old node and re-inserts a freshly constructed one with the same identity but updated `mLoadFee` and `mReportTime`.

`mLoadFee` carries the fee level that this cluster peer is currently advertising under load. `NetworkOPs` aggregates all peer load fees via `Cluster::for_each()` to compute a cluster-wide fee, which is then pushed into the fee tracker via `setClusterFee()`. This lets the network apply a consistent fee floor that reflects the busiest trusted node.

`mReportTime` uses `NetClock::time_point` — the ledger's logical network clock, not wall-clock time. `Cluster::update()` rejects a new report if `reportTime <= iter->getReportTime()`, ensuring the cluster state only advances monotonically. Using network time rather than system time avoids skew problems between nodes with different clocks.

`name_` is the human-assigned label from the config file or the peer's self-reported name in a `TMCluster` message. It is preserved across updates: if a gossiped update arrives with an empty name, `Cluster::update()` retains the previously known name rather than overwriting it with blank — a small but deliberate defensive choice.

## Construction and Defaults

The default constructor is deleted, enforcing that every `ClusterNode` must have a meaningful identity. `fee` and `rtime` default to zero/epoch respectively so that a freshly-loaded config entry (which carries no live state) starts at the lowest possible values and will immediately accept the first real update from that peer.

## Usage Patterns

The class exposes only `const` accessors. All mutation goes through `Cluster::update()`, which takes a mutex lock and performs the erase-reinsert round trip. This keeps `ClusterNode` thread-safe by design: its data is never written after construction, so any concurrent reader holding a reference obtained under the lock observes a consistent snapshot. Peers check cluster membership (`cluster()` in `PeerImp`) to decide whether to skip load-shedding, relay proposals unconditionally, and bypass signature verification on validations — making the accuracy of the cluster state operationally significant.