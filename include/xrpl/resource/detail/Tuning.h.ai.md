# `include/xrpl/resource/detail/Tuning.h`

This file is the single authoritative source of every numeric threshold and duration used by the XRPL resource-management subsystem. Rather than scattering magic numbers across the `Logic`, `Entry`, and gossip-handling code, all policy knobs live here so they can be reviewed and adjusted without hunting through implementation files.

## The Balance Thresholds

The resource manager maintains a running "balance" for every connected endpoint — a unitless score that rises as the endpoint generates load and decays exponentially over time (via the `DecayingSample` class). Two integer constants gate the two levels of enforcement:

- `warningThreshold = 5000` — when an endpoint's balance reaches this level, `Logic::disposition()` returns `Disposition::warn`. The server sends the peer a load-warning message and charges it an additional `feeWarning` penalty on the next cycle, adding a small compounding cost to continued misbehaviour.
- `dropThreshold = 25000` — five times the warning threshold. At this level `disposition()` returns `Disposition::drop`, triggering `Logic::disconnect()`, which charges `feeDrop` (making immediate reconnection costly) and severs the connection entirely.

The 5:1 ratio between warning and drop is deliberate: it gives a legitimately bursty client — one catching up on missed transactions, fetching trust lines, etc. — room to generate elevated load without being immediately disconnected, while still cutting off endpoints that sustain that load long enough to saturate the decaying window.

## Decay Window

`decayWindowSeconds = 32` is the half-life window fed as a compile-time template argument to `DecayingSample<decayWindowSeconds, clock_type>` inside `Entry`. The comment demands this value be a power of two; that is because `DecayingSample` uses bit-shifts rather than division for its decay arithmetic. Changing this value alters how quickly past charges fade: a smaller window makes the system more reactive (burst tolerance shrinks), a larger window makes it more forgiving.

## Gossip Filtering

The cluster-wide load-sharing mechanism (gossip) also relies on constants from this file:

- `minimumGossipBalance = 1000` — only inbound endpoints with a local balance at or above this level are included when `Logic::exportConsumers()` packages data for peer servers. This prevents the gossip payload from growing with every idle connection; only meaningfully loaded peers are worth advertising.
- `gossipExpirationSeconds = 30` — imported gossip records expire after 30 seconds. When they expire, `periodicActivity()` walks the import table, subtracts the remote balance contributions those records had applied to local `Entry` objects, and removes the record. This short TTL ensures that a cluster member's stale load picture cannot persistently inflate a consumer's perceived balance on other nodes.

## Inactive Entry Lifetime

`secondsUntilExpiration = 300` (five minutes) controls how long a zero-refcount `Entry` lingers in the `inactive_` list before `periodicActivity()` erases it from the main hash table. Retaining entries briefly means that a peer who disconnects and immediately reconnects can have its accumulated load recognised — it cannot reset its balance by cycling the TCP connection quickly. The much shorter gossip TTL (30 s) versus entry expiration (300 s) reflects the difference in cost: a stale local entry is cheap to keep, while stale remote load data can cause unnecessary disconnections across the cluster.

## Design Note

The integer constants are collected into a single anonymous `enum` rather than individual `constexpr int` values. This is a common C++ idiom for compile-time integer constants that avoids ODR concerns with `constexpr` variables in headers predating C++17 inline variables. The two `std::chrono::seconds` constants (`secondsUntilExpiration` and `gossipExpirationSeconds`) are `constexpr` at namespace scope because they need to be added directly to `clock_type::time_point` values, which require a typed duration rather than a bare integer.