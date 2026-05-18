# `src/xrpld/rpc/handlers/admin/peer/Peers.cpp`

## Role and Purpose

`Peers.cpp` implements `doPeers`, the admin RPC handler that responds to the `peers` command. Its job is to aggregate two distinct views of the network into a single JSON response: the real-time overlay snapshot (every TCP-connected peer) and the cluster status table (trusted cluster members and their self-reported load). It is intentionally thin — all the heavy data lives in the overlay and cluster subsystems; this handler merely assembles and formats it.

## Overlay Peer Data

The bulk of the response comes from `context.app.getOverlay().json()`, which iterates every active `PeerImp` connection and serialises its current state. Each peer entry includes fields like `complete_ledgers` and, critically, `track`. The `track` field is populated inside `PeerImp::json()` by reading the atomic `tracking_` member, which reflects how well that peer's ledger history aligns with the local node: `"diverged"` means the peer is on a different chain, `"unknown"` means consensus has not yet been established, and the absence of the field means the peer has converged.

## API Version 1 Legacy Compatibility

Modern XRPL API clients use the `track` field directly. However, API version 1 clients — the pre-2.0 interface — expected a field named `sanity` with values `"insane"` (for diverged) and `"unknown"`. The handler post-processes the overlay JSON in-place when `context.apiVersion == 1`, injecting a `"sanity"` key into each peer object that has a `track` field. The mapping is `"diverged"` → `"insane"` and `"unknown"` → `"unknown"`. Peers in the `"converged"` state produce no `track` entry at all, so they also receive no `sanity` annotation — correct for both API versions, as converged peers were simply absent from older tooling's concern.

This pattern — emit canonical output, then patch for legacy consumers before returning — keeps the overlay serialisation layer clean and avoids threading version-awareness through `PeerImp::json()`.

## Cluster Node Reporting

The second section of the response aggregates the configured cluster. `Cluster::for_each()` takes a lock on the internal `std::set<ClusterNode>` and invokes the provided lambda once per node. The handler applies several defensive guards:

- **Self-exclusion**: `node.identity() == self` skips the local node. Because the local node is normally a member of its own cluster configuration, omitting this check would cause the node to report itself among its cluster peers — misleading for operators and inconsistent with how cluster membership is used elsewhere.
- **Optional name tag**: `node.name()` is only emitted as `jss::tag` when non-empty. Cluster entries in `rippled.cfg` may or may not carry a human-readable comment, and the response omits the field rather than emitting an empty string.
- **Fee ratio**: The load fee is expressed as `node.getLoadFee() / ref` — a floating-point multiplier relative to the network's `loadBase`. Only emitted when it differs from the reference and is non-zero, which avoids polluting the output with a `1.0` ratio for every normal node. A value greater than `1.0` signals that a cluster peer is under load and applying a fee premium.
- **Age in seconds**: `node.getReportTime()` holds the `NetClock::time_point` at which the cluster node last broadcast its status. If that time is the zero-constructed default (never reported), the `age` field is suppressed entirely. Otherwise, age is computed as `(now - reportTime).count()`, clamped to zero for the unlikely case where `reportTime >= now` (clock skew guard).

## Relationships

`doPeers` has no local state or concurrency concerns of its own. It delegates to three subsystems: `Overlay` for live peer data, `Cluster` for trusted-peer metadata, and `TimeKeeper` for the current network clock. The `LoadFeeTrack::getLoadBase()` call provides the denominator for fee normalisation. The handler itself is stateless — it reads application-wide singletons through the `RPC::JsonContext` handle and returns a `Json::Value` by value, which is the standard idiom for all admin RPC handlers in this directory.