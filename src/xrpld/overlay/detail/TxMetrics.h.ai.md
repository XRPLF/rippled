# `TxMetrics.h` — Rolling-Average Metrics for Transaction Reduce-Relay

This header defines the `xrpl::metrics` namespace structures that underpin observable telemetry for the **transaction reduce-relay** feature. The reduce-relay mechanism deliberately withholds redundant transaction relays to peers that already have a transaction, cutting network traffic at the cost of needing careful instrumentation. These structures give operators visibility into how aggressively the feature suppresses relays and how the resulting protocol message traffic compares to an unconstrained baseline.

## Three-Layer Abstraction

The design nests cleanly into three levels of increasing specificity.

### `SingleMetrics` — The Primitive Rolling Average

`SingleMetrics` tracks a single numeric stream. On every call to `addMetrics(val)` it accumulates `val` into `accum` and increments the sample count `N`. Once at least one second has elapsed since `intervalStart`, it computes a per-interval value and resets:

- If `perTimeUnit` is `true`, the interval value is `accum / elapsed_seconds` — a **throughput rate** (bytes per second, messages per second).
- If `perTimeUnit` is `false`, the interval value is `accum / N` — a **sample mean** (average peers per transaction decision).

The interval value is pushed into a `boost::circular_buffer<uint64_t>` of capacity 30. The published `rollingAvg` is the mean of whatever is currently in that buffer. Because the buffer is pre-filled with zeros, the first 30 one-second windows are dampened downward — a deliberate smoothing artifact rather than a bug. Over steady-state operation the buffer represents roughly 30 seconds of history.

The choice of `perTimeUnit` as a constructor flag (defaulting `true`) means the same struct handles both classes of measurement without subtyping or templates.

### `MultipleMetrics` — Count/Size Pairing

`MultipleMetrics` holds two `SingleMetrics` instances (`m1` and `m2`) and provides two `addMetrics` overloads. When called with a single argument `addMetrics(val2)`, it synthesizes a count of 1 for `m1` automatically — this is the typical protocol-message path, where the caller knows only the byte size and the count-per-second is implicit. When called with two arguments `addMetrics(val1, val2)`, both are forwarded explicitly. In practice `m1` always tracks message count and `m2` tracks byte size.

### `TxMetrics` — The Top-Level Aggregate

`TxMetrics` holds a `MultipleMetrics` instance for each tracked protocol message type:

| Field | Protocol Message |
|---|---|
| `tx` | `mtTRANSACTION` |
| `haveTx` | `mtHAVE_TRANSACTIONS` |
| `getLedger` | `mtGET_LEDGER` |
| `ledgerData` | `mtLEDGER_DATA` |
| `transactions` | `mtTRANSACTIONS` |

It also carries three `SingleMetrics` with `perTimeUnit=false` that capture the relay-decision outcome per transaction event: `selectedPeers` (peers chosen to receive the relay), `suppressedPeers` (peers that were skipped), and `notEnabled` (peers with reduce-relay disabled). These are sample averages, not rates, because the interesting signal is the ratio of selected to suppressed per transaction, not the absolute volume over time.

A fourth `SingleMetrics` (`missingTx`, `perTimeUnit=true`) tracks how frequently `TMTransactions` bundles arrive carrying transaction data that the local node didn't have — a proxy for how much network load the reduce-relay is generating when peers must reactively request missing transactions.

## Concurrency Model

`TxMetrics` owns a `mutable std::mutex mutex`. All three `addMetrics` overloads on `TxMetrics` acquire this lock before touching any `SingleMetrics` or `MultipleMetrics` state. `json()` also holds the lock while reading every `rollingAvg` field, since those reads are not atomic.

Importantly, `SingleMetrics` and `MultipleMetrics` themselves are **not** independently thread-safe — the lock lives one level up at `TxMetrics`. In `OverlayImpl`, the `txMetrics_` member is additionally guarded by the overlay's Boost.Asio strand: `addTxMetrics()` posts to the strand if the caller is not already on it. This creates a two-level protection scheme — the strand serializes writes from peer I/O threads, while the mutex protects against concurrent reads by RPC threads that call `txMetrics()` on a different execution context.

## Integration and Exposure

`OverlayImpl` holds a single `metrics::TxMetrics txMetrics_` and exposes it two ways:

- `addTxMetrics(...)` — variadic template that forwards to `txMetrics_.addMetrics(...)`, called from `PeerImp::onMessageBegin()` (on every tracked inbound message) and from the relay-decision code in `OverlayImpl` whenever a transaction is dispatched to the peer set.
- `txMetrics()` — implements the pure virtual `Overlay::txMetrics() const`, returning `txMetrics_.json()`.

Metric collection is gated on the `TX_REDUCE_RELAY_ENABLE` or `TX_REDUCE_RELAY_METRICS` config flags, so it can be enabled for diagnostic purposes on nodes that are not yet enforcing the relay reduction.

## JSON Output

`TxMetrics::json()` produces a flat `Json::Value` object with 13 fields, all registered in `jss.h` under `txr_*` names. A quirk worth noting: every value is serialized via `std::to_string` and stored as a **JSON string**, not a number. Consumers (such as the `get_counts` RPC command) must parse these strings if they need numeric comparison.