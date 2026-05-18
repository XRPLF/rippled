# `TxMetrics.cpp` — Transaction Reduce-Relay Metrics

## Purpose

This file implements the metrics collection subsystem for XRPL's **transaction reduce-relay** feature — a peer-selection optimization that suppresses redundant transaction forwarding across the overlay network. Rather than flooding every connected peer, the relay logic selects a reduced set of targets, and these metrics track the effectiveness and volume of that decision-making in real time.

The file defines three cooperating types in the `xrpl::metrics` namespace: `SingleMetrics` (a one-dimensional rolling average accumulator), `MultipleMetrics` (pairs two `SingleMetrics` instances to track both count and byte size for a single message class), and `TxMetrics` (the top-level aggregator owning one `MultipleMetrics` per monitored protocol message type plus several standalone `SingleMetrics` for peer-selection diagnostics).

## `SingleMetrics` — Rolling Average Engine

`SingleMetrics::addMetrics()` is the computational core of the entire file. Its design reflects a deliberate tradeoff between accuracy and overhead: rather than computing a continuous moving average on every call, it amortizes the update into 1-second intervals.

On each call the incoming `val` is added to `accum` and the sample counter `N` is incremented. When a `std::chrono::steady_clock` check reveals that at least one second has elapsed, the accumulator is condensed into a single average for that interval and pushed into a `boost::circular_buffer<uint64_t>` with capacity 30. The overall `rollingAvg` is then recomputed as the arithmetic mean of all values in that buffer before `accum`, `N`, and `intervalStart` are reset.

The `perTimeUnit` flag selects between two averaging modes. When `true` (the default), the divisor is the elapsed integer seconds, giving a **rate per second** — appropriate for byte and message counts that accumulate continuously. When `false`, the divisor is `N` (the sample count in the interval), giving a **per-observation average** — appropriate for quantities like "how many peers were selected for this transaction", where each call represents one independent decision rather than contributing to a cumulative rate.

The 30-slot circular buffer means the `rollingAvg` reflects the trailing 30-second history. Slots default to `0ull`, so a window with no activity naturally dilutes the average toward zero as non-zero entries age out — there is no explicit decay or reset; the circular buffer handles it structurally.

## `MultipleMetrics` — Count/Size Pairs

`MultipleMetrics` exists purely as a convenience wrapper around two `SingleMetrics` (`m1` for count, `m2` for bytes). The two-argument `addMetrics(val1, val2)` delegates straight through; the one-argument overload hardcodes `val1 = 1`, which is the expected pattern for protocol messages: each received message increments the count by one while contributing a variable byte size. Callers like `PeerImp` record message arrivals this way without needing to assemble both values at the call site.

## `TxMetrics` — Aggregator and JSON Export

`TxMetrics` aggregates five `MultipleMetrics` members covering the protocol message types most relevant to relay decisions: `mtTRANSACTION`, `mtHAVE_TRANSACTIONS`, `mtGET_LEDGER`, `mtLEDGER_DATA`, and `mtTRANSACTIONS`. Unknown message types pass through `addMetrics(type, val)`'s `switch` silently — the `default: return` avoids any error path, which is correct since new message types should not crash metrics collection.

The three `SingleMetrics` instances for peer selection — `selectedPeers`, `suppressedPeers`, and `notEnabled` — are all constructed with `perTimeUnit=false`, giving per-transaction-event averages rather than per-second rates. This makes diagnostic sense: the operator wants to know "on average, how many peers does each tx touch or skip?" not "how many peers per second?". `missingTx`, by contrast, uses the default `perTimeUnit=true` to report missing-transaction requests as a frequency.

`json()` serializes all thirteen `rollingAvg` values through `jss::` field-name constants (e.g., `jss::txr_tx_cnt`, `jss::txr_selected_cnt`). All values are stringified via `std::to_string` rather than stored as JSON integers — a minor quirk that consumers must account for.

## Thread Safety

`TxMetrics` holds a `mutable std::mutex` and every public method (including the `const json()`) acquires a `std::lock_guard` before touching any metric state. The subordinate `SingleMetrics` and `MultipleMetrics` types have no locks of their own; they rely entirely on `TxMetrics`' mutex.

In practice, `OverlayImpl` owns a `TxMetrics txMetrics_` member and wraps all mutations in a variadic `addTxMetrics<Args...>()` template that posts to an Asio strand when called off-strand. This means most write-path calls arrive serialized on the strand without contention, and the mutex primarily guards the read path — `json()` can be invoked by any RPC-handling thread independently of the strand. The two-layer design (strand for writers, mutex for reader/writer arbitration) avoids requiring RPC threads to post their query through the strand, at the cost of one additional lock acquisition per report.