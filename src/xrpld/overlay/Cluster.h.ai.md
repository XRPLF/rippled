# `xrpld/overlay/Cluster.h` — Trusted Cluster Node Registry

`Cluster` manages the set of validator or relay nodes belonging to a trusted operator cluster in the XRPL peer-to-peer overlay. In XRPL's architecture, a "cluster" is a group of nodes run by the same administrative entity. Members can propagate state to each other efficiently and are treated with elevated trust — for example, exempted from load-based fee throttling that applies to anonymous peers. This class is the single authority for cluster membership queries and state maintenance throughout the overlay layer.

## Data Model

Internally, cluster members are held in a `std::set<ClusterNode, Comparator>`. The choice of `std::set` over `std::unordered_set` is deliberate: it enables stable iteration order and, more importantly, supports the heterogeneous lookup trick discussed below. Each `ClusterNode` (defined in `ClusterNode.h`) carries four fields: an immutable `PublicKey` identity, a human-readable name/comment string, a load fee integer, and a `NetClock::time_point` recording when the node last reported its state. The public key serves as the unique identifier and sort key.

## Transparent Comparator and Heterogeneous Lookup

The private `Comparator` struct is the most architecturally interesting piece of this header. It provides three overloads of `operator()`: `(ClusterNode, ClusterNode)`, `(ClusterNode, PublicKey)`, and `(PublicKey, ClusterNode)`. By tagging the struct with `using is_transparent = std::true_type;`, it opts into C++14 heterogeneous associative container lookup. This means `nodes_.find(identity)` in `member()` and `update()` accepts a raw `PublicKey` directly rather than requiring construction of a dummy `ClusterNode` object. Without this, every membership check would require allocating a temporary node just to perform the lookup — wasteful and semantically awkward since partial construction of a `ClusterNode` would be needed (its constructor is `delete`d for the default case).

## Thread Safety

All public methods are guarded by a `mutable std::mutex`. The `mutable` qualifier on both `mutex_` and `j_` is required because `member()`, `size()`, and `for_each()` are `const` methods that still need to acquire the lock and log. The `for_each()` method holds the lock for its entire iteration pass and explicitly documents that calling `update()` from within the callback is forbidden — doing so would deadlock on the non-recursive mutex.

## Update Semantics and the Erase-Reinsert Pattern

`update()` enforces a monotonic-time invariant: it only applies a new state record if `reportTime` is strictly newer than the stored report time. This guards against stale gossip from cluster peers arriving out of order. Because `std::set` elements are logically `const` after insertion (you cannot modify a key in-place without corrupting the container's ordering), updating a node requires erasing the old entry and reinserting a new one. The implementation captures the iterator hint from `erase()` and passes it to `emplace_hint()`, making the reinsert O(1) amortized rather than O(log n). There is also a name-preservation rule: if the incoming `name` argument is empty, the existing name is carried forward, so a status update from a cluster peer that omits a name does not silently blank out the human-readable label loaded from config.

## Loading from Configuration

`load()` parses a `[cluster_nodes]` config section where each line is a base58-encoded node public key optionally followed by a whitespace-separated comment. It uses a compiled `static boost::regex` (initialized once) to tokenize each line, then calls `parseBase58<PublicKey>` with `TokenType::NodePublic` to decode the key. Duplicate entries produce a warning but are skipped rather than failing the load — the first entry wins. Malformed lines or invalid public keys return `false` immediately, signalling the application startup to abort or warn.

## Relationship to `ClusterNode`

`ClusterNode` is a simple value type — no virtual methods, no shared state. `Cluster` owns all `ClusterNode` instances exclusively inside its `std::set`. The separation of `ClusterNode.h` from `Cluster.h` allows other parts of the overlay (e.g., `PeerImp`) to accept `ClusterNode const&` in callbacks via `for_each()` without pulling in the full cluster management machinery.