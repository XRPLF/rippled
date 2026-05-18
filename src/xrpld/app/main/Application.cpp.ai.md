# `src/xrpld/app/main/Application.cpp`

This file is the structural heart of the `xrpld` server process. It implements `ApplicationImp`, the concrete realization of the `Application` abstract interface, which acts as the global service registry and lifecycle manager for every significant subsystem in the XRPL node. Every component — from the ledger and consensus engine to the peer overlay, RPC handler, gRPC server, and transaction queue — is constructed, started, and stopped through this single class.

## Class Hierarchy and Ownership Model

`ApplicationImp` inherits from both `Application` (which itself extends `ServiceRegistry` and `beast::PropertyStream::Source`) and `BasicApp`. `BasicApp` is a slim wrapper that owns the `boost::asio::io_context` and a thread pool that drives it. The design makes the `io_context` outlive all children by keeping it at the `BasicApp` layer, above the subsystems that schedule work onto it.

The `Application` interface exposes pure virtual accessors for every major subsystem — `getLedgerMaster()`, `getOPs()`, `getOverlay()`, etc. — making `ApplicationImp` the canonical service locator. Almost every subsystem receives an `Application&` reference to `*this` at construction, enabling cross-component access through a single stable pointer rather than individual dependency injection into each relationship. This is a conscious tradeoff: it creates tight coupling at the application layer while keeping subsystem interfaces narrow.

The sole public creation mechanism is `make_Application()`, a factory that constructs an `ApplicationImp` via `std::make_unique`. The implementation class is never exposed in the header.

## The Constructor's Deliberate Inertia

`ApplicationImp`'s constructor is dominated by a massive member initializer list that instantiates every subsystem. A critical design rule is enforced here, explicitly documented in the constructor body: **no threads, sockets, or real I/O work may start in the constructor**. That responsibility belongs to `setup()` (configuration and database work) and `start()` (service activation). The reason is that unit tests construct `Application` objects without running them, so anything that starts in the constructor would fail to stop cleanly.

The constructor also generates `instanceCookie_`, a random 64-bit value used to distinguish each process incarnation. The CSPRNG is seeded externally before this call.

The `JobQueue` worker count is computed inline during construction using a lambda. It scales with `NODE_SIZE` and `hardware_concurrency`: small systems get 2–6 workers; "large" (size ≥ 4) systems with 16+ cores get up to 14. Similarly, the I/O thread count for `BasicApp` tops out at 6, reduced to 1 for under-provisioned or single-core systems.

## `io_latency_sampler`: The Heartbeat Monitor

An inner class, `io_latency_sampler`, wraps `beast::io_latency_probe` to measure round-trip dispatch latency on the `io_context`. Every 100ms it posts a probe job; the elapsed time before the probe is picked up is the IO latency. Values ≥ 10ms are emitted as telemetry events; values ≥ 500ms produce a warning log. This is the primary observable signal for detecting when the event loop is overloaded — a critical concern because the consensus engine posts time-sensitive work here.

## `setup()`: Configuration to Live State

`setup()` is a long sequential initialization that transforms a loaded `Config` object into a running-but-not-yet-serving application state. It is approximately 400 lines and is flagged with a TODO to break it up.

The sequence proceeds as: signal handler registration (SIGINT/SIGTERM → `signalStop()`) → database initialization → peer reservation loading → validator max-ledger fence (`setMaxDisallowedLedger()`) → amendment table construction → pathfinder table initialization → ledger startup mode selection → order book setup → node identity loading → cluster config → validator manifests and trusted validator list → validator sites → overlay (peer network) creation → first consensus round → server handler (RPC/WebSocket ports) setup and port fixup → standalone/network mode → `[rpc_startup]` command execution → validator site refresh start.

The ledger startup mode is a fork:
- `Fresh` — creates a brand new genesis ledger with all currently desired amendments enabled.
- `Load` / `LoadFile` / `Replay` — calls `loadOldLedger()`, falling back to a fresh genesis if `FAST_LOAD` is set and the ledger cannot be found.
- `Network` — starts from a genesis but immediately flags the node as needing to acquire the network ledger.

The amendment table is constructed from `detail::supportedAmendments()`, enriched by the `[amendments]` (vote yes) and `[veto_amendments]` (vote no) config sections, and then linked to the current validator quorum keys via `trustChanged()`.

## `start()` and the Ordered Activation Problem

`start(bool withTimers)` activates services in a deliberate sequence. The `withTimers` parameter suppresses sweep and entropy timers in unit test mode. After timers, each service's `start()` is called: IO latency sampler, DNS resolver, load manager, SHAMapStore, overlay, gRPC server, ledger cleaner, perf log.

The gRPC server is notable: after `grpcServer_->start()`, another call to `fixConfigPorts` updates the config with the gRPC port (in case port 0 was used for auto-assignment). `fixConfigPorts()` at the bottom of the file solves the bootstrap problem where port configuration must be known before binding, but the actual bound port may differ (e.g., when `port = 0` triggers OS port assignment). It walks the live `Endpoints` map and back-fills the actual port numbers into the `Config` sections.

## `run()`, `signalStop()`, and the Stop Protocol

`run()` is the blocking main loop. After optionally arming the stall detector, it executes:

```cpp
isTimeToStop.wait(false, std::memory_order_relaxed);
```

This is C++20 atomic_flag waiting — a lock-free futex-like block until `signalStop()` sets the flag and calls `notify_all()`. `signalStop()` uses `test_and_set()` to ensure the stop message is logged exactly once regardless of how many threads call it concurrently — the system can receive SIGTERM, a DB space check failure, and a PerfLog timeout all simultaneously.

The shutdown sequence that follows is carefully ordered and documented as fragile. `waitHandlerCounter_.join()` blocks until all pending Asio timer callbacks (sweep, entropy) drain — this is essential because those handlers capture `this` and would access destroyed members if allowed to fire after `run()` returns. Then manifests are persisted, and services are stopped in reverse dependency order: load manager, SHAMapStore, job queue, overlay, gRPC, NetworkOPs, server handler, ledger replayer, inbound transactions, inbound ledgers, ledger cleaner, node store, perf log.

## `doSweep()`: Cache Pressure Management

The sweep timer fires at a configurable interval (defaulting to a `SizedItem` value based on node size). `doSweep()` iterates through every major cache — `NodeFamily`, `TransactionMaster`, `LedgerMaster`, `TempNodeCache`, `RCLValidations`, `InboundLedgers`, `LedgerReplayer`, `AcceptedLedgerCache`, `CachedSLEs` — calling each one's sweep or expire method and logging the before/after size at debug level. It also checks whether the transaction database has sufficient disk space remaining and calls `signalStop()` if it does not. After all sweeps, `mallocTrim()` returns freed memory to the OS, counteracting the tendency of `malloc` arenas to hold pages indefinitely. The timer is rescheduled by `doSweep()` itself at the end, creating a chain rather than a fixed interval.

## `loadOldLedger()` and Replay Mode

`loadOldLedger()` is the most defensively-written function in the file. It accepts a ledger identifier that could be a 64-char hex hash, a decimal sequence number, the string `"latest"`, a filename, or empty. After locating the ledger in the relational or node store (falling back to `InboundLedger::checkLocal()` for raw node-store recovery), it validates integrity in three layers: the account hash must be non-zero, `walkLedger()` must find all nodes present, and `isSensible()` must pass. Any failure triggers `UNREACHABLE` with `LCOV_EXCL` markers indicating the paths are not expected in tests.

In replay mode, the function also loads the parent ledger, builds a `LedgerReplay` structure, and injects all transactions from the replay target into the open ledger using `rawTxInsert`. An optional `trapTxID` can mark a specific transaction for halting — a debug feature for instrumenting consensus replays.

## `setMaxDisallowedLedger()` and Validator Safety

For a freshly-started validator, re-signing proposals for ledgers that were already closed and persisted before a crash could cause equivocation. `setMaxDisallowedLedger()` queries the maximum ledger sequence from the relational database at startup and stores it as an `std::atomic<LedgerIndex>`. NetworkOPs uses this value to refuse signing proposals for ledger sequences at or below it, providing a safety fence around restart-driven double-signing.

## `serverOkay()`: Health Gate for Load Balancers

When `ELB_SUPPORT` is enabled in config, `serverOkay()` gates the HTTP 200 response used by load balancers. It returns false (with a human-readable reason) if the server is stopping, hasn't yet acquired the network ledger, is amendment-blocked (too old to understand current transactions), has no valid validator list (UNL-blocked), is not yet syncing, has fallen behind on ledgers, or is locally overloaded. The ordering matters — amendment-blocked and UNL-blocked checks appear before the sync check because a node stuck in those states should be drained from the pool regardless of apparent sync status.