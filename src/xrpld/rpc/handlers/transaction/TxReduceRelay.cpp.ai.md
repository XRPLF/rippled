# `TxReduceRelay.cpp`

This file is the RPC handler for the `tx_reduce_relay` command — a read-only introspection endpoint that surfaces transaction relay performance metrics from the node's peer overlay network.

The entire implementation is a single line: `doTxReduceRelay` accepts the standard `RPC::JsonContext` and immediately delegates to `context.app.getOverlay().txMetrics()`. There is no input parsing, no parameter validation, and no transformation of the result — the `Json::Value` produced by `Overlay::txMetrics()` is returned directly to the caller.

The handler is registered in `Handler.cpp` under the `"tx_reduce_relay"` command name with `Role::USER` access and `NO_CONDITION`, meaning any connected client can query it without special privileges or an active network connection requirement.

The data it exposes comes from `metrics::TxMetrics` (declared in `overlay/detail/TxMetrics.h`), a struct maintained by `OverlayImpl` that tracks rolling-average statistics for the reduce-relay subsystem: bytes and message counts per second for `TMTransaction`, `TMHaveTransactions`, `TMGetLedger`, `TMLedgerData`, and `TMTransactions` protocol messages; per-transaction sample averages for selected, suppressed, and feature-disabled peers; and a rate of missing transaction requests. These counters are populated deep in `OverlayImpl`'s transaction forwarding logic, where the relay algorithm decides which subset of peers to send each transaction to rather than flooding all connected peers unconditionally.

The deliberate minimalism here is by design. The handler's only job is to provide a transparent window into the overlay's internal accounting without owning any of that state itself. All thread-safety and data correctness concerns live in `TxMetrics` (which holds its own `mutex`) and `OverlayImpl`.