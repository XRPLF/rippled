# `Counts.h` — Peer Slot Accounting for PeerFinder

`Counts` is a pure bookkeeping class that tracks the current occupancy of every connection-slot category managed by the PeerFinder subsystem. It answers the resource-management questions that `Logic.h` needs in order to make policy decisions: Are there free inbound slots? Should more outbound attempts be initiated? Can this newly-handshaked slot be promoted to active? Without accurate counts, the connection manager would be blind to its own state.

## Relationship to `Logic` and `Slot`

`Logic` (in `Logic.h`) holds a single `Counts counts_` member alongside the `Slots` map. Every time a slot is created, transitions states, or is destroyed, `Logic` calls `counts_.add()` or `counts_.remove()` — or both in sequence when a slot's state changes. `Logic` guards all these operations under its own `std::recursive_mutex lock_`, so `Counts` itself carries no synchronization. This is a deliberate design choice: `Counts` is a value-semantics helper embedded inside a larger guarded object, and giving it its own mutex would create nested-lock complexity for no benefit.

The `Slot` abstract interface (from `Slot.h`) exposes the three properties `Counts` needs — `state()`, `inbound()`, `fixed()`, and `reserved()` — without exposing mutable internals.

## The `adjust()` Core

All counting logic funnels through the private `adjust(Slot const& s, int n)` method, where `n` is `+1` for additions and `-1` for removals. `add()` and `remove()` are thin wrappers that call `adjust` with the appropriate sign. This single-entry-point pattern ensures that every counter that tracks a given slot property is updated together, making it impossible for add and remove to diverge in the fields they touch.

The `adjust()` method interprets the slot's state enum (`accept`, `connect`, `connected`, `active`, `closing`) with a switch statement and updates the corresponding counters. The assertions embedded in the cases enforce expected invariants: `accept` state must be inbound, while `connect` and `connected` states must be outbound. These are caught in debug builds where they would otherwise silently corrupt the counts.

## The Fixed/Reserved Carve-Out

The most architecturally significant design choice in `Counts` is how fixed and reserved connections are treated. Fixed peers — those explicitly listed in the node's configuration — and reserved peers (cluster members, peers with explicit reservations) are tracked in separate counters (`m_fixed`, `m_fixed_active`, `m_reserved`) and are explicitly excluded from the inbound/outbound active tallies used to enforce slot limits. This appears in two places:

- In `adjust()`, the `active` case only increments `m_in_active` or `m_out_active` when `!s.fixed() && !s.reserved()`.
- In `can_activate()`, if `s.fixed() || s.reserved()` is true, the method returns `true` unconditionally, bypassing the limit check against `m_in_active`/`m_out_active`.

The rationale is that fixed and reserved connections represent administrative intent — the operator explicitly wants these peers — so they must not be blocked by capacity policies that apply to ordinary public peers. Fixed peers are also what allow a private cluster to form before the public peer quota fills up.

## State Transition Accounting

The `Slot::State` enum defines five states: `accept`, `connect`, `connected`, `active`, `closing`. Of these:

- `accept` — inbound connection pending handshake — increments `m_acceptCount`.
- `connect` and `connected` — outbound attempt in progress — both increment `m_attempts`, the outbound connection attempt counter.
- `active` — fully established peer — increments `m_active`, plus conditionally `m_in_active`, `m_out_active`, and `m_fixed_active` as appropriate.
- `closing` — graceful teardown — increments `m_closingCount`.

When `Logic` transitions a slot between states, it calls `counts_.remove(*slot)` with the old state, then transitions the slot, then calls `counts_.add(*slot)` with the new state. This sequence ensures counts are always consistent with the slot map.

## Connection Policy Gates

`can_activate()` is the primary admission-control gate. Before promoting a slot from `connected` or `accept` to `active`, `Logic` calls this method. For non-fixed, non-reserved connections, it enforces `m_in_active < m_in_max` (inbound) or `m_out_active < m_out_max` (outbound). The limits themselves come from the `Config` object via `onConfig()`, which sets `m_out_max = config.outPeers` and `m_in_max = config.inPeers` (only if `config.wantIncoming` is set; otherwise `m_in_max` stays at its zero default, rejecting all inbound slots).

`attempts_needed()` computes how many more outbound connection attempts `Logic` should launch by comparing `m_attempts` against `Tuning::maxConnectAttempts` (20). This caps the in-flight connection storm — the node will not initiate more than 20 simultaneous outbound attempts regardless of how many free slots remain.

`isConnectedToNetwork()` has a subtle implementation: it returns `true` only when `m_out_max == 0`. This reflects the specific edge case where a node is configured with zero desired outbound connections (e.g., a pure listener), in which case it considers itself "connected" without needing to establish any outbound peers. In the common case where `m_out_max > 0`, the method always returns `false`, and `Logic` uses other state (comparing `m_out_active` against `m_out_max`) to decide whether to drive more connections.

## Diagnostics

`onWrite()` serialises the current state into a `beast::PropertyStream::Map`, producing labelled fields `accept`, `connect`, `close`, `in` (as `active/max`), `out` (as `active/max`), `fixed`, `reserved`, and `total`. `state_string()` returns a compact human-readable summary (e.g., `"3/8 out, 10/21 in, 2 connecting, 0 closing"`) used in log messages throughout `Logic`. Both methods are read-only and carry no side effects, making them safe to call for monitoring at any point.