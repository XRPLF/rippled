# `Tuning.h` — System-Wide Performance Constants

`Tuning.h` is a deliberate single-location home for hard-coded performance constants that affect multiple subsystems in the XRPL node. Rather than scattering magic numbers across `NodeFamily.cpp`, `NetworkOPs.cpp`, and whatever else grows to depend on them, this header provides named, typed constants in the `xrpl` namespace so their intent is explicit and their values are easy to review or adjust in one place.

## `fullBelowTargetSize` and `fullBelowExpiration`

These two constants configure the `FullBelowCache` instantiated in `NodeFamily::NodeFamily()`. The "full below" concept is specific to SHAMap synchronization: when a node can confirm that every descendant of a given inner node is already present in its local store, that inner node's hash is inserted into this cache. Subsequent sync passes consult the cache first — if a child hash `touch_if_exists()` hits, the entire subtree is skipped, avoiding redundant traversal of potentially thousands of nodes already known to be complete.

`fullBelowTargetSize = 524288` sets the capacity target at 512 Ki entries (512 × 1024). This is deliberately large because the cache must cover hashes from the current ledger's state tree and transaction tree simultaneously, across potentially millions of accounts and ledger objects. Undersizing it would cause thrashing: recently confirmed subtrees would evict each other, turning cache hits back into full traversals.

`fullBelowExpiration = std::chrono::minutes{10}` controls how long a "full below" record is trusted. Because ledger state mutates with every new ledger close (typically every 3–4 seconds), a subtree that was complete ten minutes ago may no longer be complete for the current in-progress sync. Ten minutes is a pragmatic choice: long enough to stay warm across the natural sweep interval that calls `fbCache_->sweep()`, short enough that stale entries from much-older ledger generations do not permanently pollute the cache.

Notably, the `TreeNodeCache` — the other cache in `NodeFamily` — does not use constants from `Tuning.h`. Its size and age come from `app.config().getValueFor(SizedItem::treeCacheSize/treeCacheAge)`, reflecting operator-tunable values. The full-below cache, by contrast, is an internal SHAMap sync optimization with no operator-facing knob; fixed constants are appropriate there.

## `maxPoppedTransactions`

`maxPoppedTransactions = 10` bounds the work done in a single "cascade submit" pass inside `NetworkOPs`. After a transaction is successfully applied to the open ledger, the node checks whether the same account has additional pending transactions queued up with the next sequential sequence numbers or tickets. If so, it eagerly pops them — up to 10 — and submits them as a batch in the same processing cycle.

The limit exists to prevent a single productive account from monopolizing the processing loop. An account could in theory have hundreds of queued transactions; without this cap, the loop iterating with `popAcctTransaction()` would consume unbounded time while the batch lock remains contested. By limiting each triggered cascade to 10 transactions, the scheduler ensures fairness across accounts and keeps per-iteration latency predictable.

## Design Rationale

Centralizing these constants in `app/main/Tuning.h` — even though their consumers live in `shamap/` and `app/misc/` — reflects a deliberate architectural choice: performance-sensitive magic numbers should be discoverable from a single file rather than embedded in constructors or loop conditions. Any engineer tuning node performance under load knows to look here first. The use of `constexpr` (rather than `#define`) ensures they are type-safe, respect namespace scoping, and can be used in `if constexpr` contexts without macro-hazard side effects.