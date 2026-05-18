# `PeerImp.h` — Active Peer Connection Management

`PeerImp` is the concrete implementation of a live peer-to-peer connection in the XRPL overlay network. Where the abstract `Peer` interface and `Overlay` facade express *what* the network can do, `PeerImp` is *how* a single connected node actually behaves: it owns the SSL/TCP socket, drives the asynchronous read/write loops, dispatches every incoming protobuf message to the right handler, tracks ledger convergence, enforces resource budgets, and orchestrates a careful multi-stage shutdown when the connection ends. Every validator, full-history node, and ordinary rippled peer that connects to a running node is eventually managed as a `PeerImp`.

## Inheritance and Ownership

The class inherits from three bases simultaneously:

- **`Peer`** — the public interface consumed by the rest of the application (consensus, ledger acquisition, broadcast). All calls from outside the overlay go through this pure-virtual handle.
- **`std::enable_shared_from_this<PeerImp>`** — mandatory because virtually every async callback captures `shared_from_this()` to keep the peer alive across I/O completion. The workaround comment `// Work-around for calling shared_from_this in constructors` explains why `run()` is virtual and deferred rather than called in the constructor body.
- **`OverlayImpl::Child`** — registers the peer with the overlay manager so that `stop()` can be broadcast to all children. The `~PeerImp` destructor calls `overlay_.deletePeer()`, `overlay_.onPeerDeactivate()`, and `overlay_.peerFinder().on_closed()` in sequence, cleaning up the PeerFinder slot and traffic tracking before destruction completes.

## Dual-Constructor Pattern

Two constructors exist for different connection directions:

- The **inbound** constructor accepts a completed HTTP upgrade request (`http_request_type&&`), reads feature headers from `request_`, and sets `inbound_ = true`. `run()` subsequently calls `doAccept()`.
- The **outbound** constructor (template on `Buffers`) accepts the HTTP response from the remote side, reads feature headers from `response_`, pre-populates `read_buffer_` from any bytes already received during the handshake, and sets `inbound_ = false`. `run()` calls `doProtocolStart()` directly.

Both paths resolve feature negotiation at construction time by inspecting HTTP headers for `FEATURE_COMPR` (lz4 compression), `FEATURE_TXRR` (transaction reduce-relay), `FEATURE_LEDGER_REPLAY`, and `FEATURE_VPRR` (validator-proposal reduce-relay base squelch). The result is stored in three boolean/enum fields (`compressionEnabled_`, `txReduceRelayEnabled_`, `ledgerReplayEnabled_`) that remain immutable for the life of the connection.

## Transport Layer

The type stack is:
```
socket_type (TCP socket)
  → middle_type (beast::tcp_stream)
    → stream_type (beast::ssl_stream<middle_type>)
```

`stream_ptr_` owns the SSL stream as a `unique_ptr`. The references `socket_` and `stream_` are kept as plain references after construction to avoid repeated pointer dereferences in the hot I/O path. All async operations are serialized through `strand_`, a `boost::asio::strand<executor>` that guarantees sequenced execution of callbacks without a mutex — the primary thread-safety mechanism for all connection-level state.

## Message Dispatch

The protocol message loop is driven by `doProtocolStart()` → `onReadMessage()`. Each call to `onReadMessage` consumes bytes from `read_buffer_` (a `boost::beast::multi_buffer`), parses one or more protobuf frames, and dispatches to the appropriate overloaded `onMessage()`. Twenty distinct message types are handled, including:

- **`TMTransaction` / `TMTransactions`** — single and batched transaction relay, routed through `handleTransaction()` which calls `checkTransaction()` for signature verification and fee-based routing
- **`TMProposeSet` / `TMValidation`** — consensus proposals and validations, dispatched to `checkPropose()` and `checkValidation()` respectively after trust checks
- **`TMGetLedger` / `TMLedgerData`** — ledger data requests and responses, handled by `processLedgerRequest()` which calls `getLedger()` or `getTxSet()` depending on the request mode
- **`TMEndpoints`** — peer discovery endpoints propagated through PeerFinder
- **`TMSquelch`** — reduce-relay control messages that call `squelch_.addSquelch()` or `squelch_.removeSquelch()` to suppress redundant validator message forwarding
- **`TMHaveTransactions` / `TMTransactions`** — the transaction reduce-relay protocol: peers advertise unheld hashes, and the receiver fetches what it's missing via `handleHaveTransactions()` / `doTransactions()`
- **`TMProofPathRequest/Response` / `TMReplayDeltaRequest/Response`** — ledger replay messages, delegated to `ledgerReplayMsgHandler_`

`onMessageBegin()` and `onMessageEnd()` bracket each dispatch for metrics accounting: `metrics_.recv` accumulates incoming byte counts in a 30-slot circular rolling average.

## Tracking State Machine

`tracking_` is a `std::atomic<Tracking>` with three values: `converged`, `unknown`, and `diverged`. The `checkTracking()` overloads compare a recently-validated ledger sequence against the peer's advertised `closedLedgerHash_`. From `Tuning.h`, a peer within 24 ledgers is converged; beyond 128 ledgers it is diverged. The atomic avoids locking in the hot validation-received path. `trackingTime_` records when the state last changed; unknown-state peers that remain unknown too long will be disconnected by the timer handler.

## Shutdown State Machine

The shutdown design is the most architecturally intricate part of the class. Two boolean flags coordinate the process:

- **`shutdown_`**: set the moment any shutdown trigger fires — signals all pending I/O to drain rather than start new work
- **`shutdownStarted_`**: set when the SSL `async_shutdown` is actually posted — prevents double-initiation

The progression is:

```
stop() / fail() / onTimer()
  → shutdown()          — sets shutdown_, cancels peer timer, posts 5s safety timer
    → tryAsyncShutdown()— gates on !readPending_ && !writePending_
      → stream_.async_shutdown()
        → onShutdown()  — cancels safety timer, calls close()
          → close()     — socket_.close(), notifies overlay
```

`tryAsyncShutdown()` is the key gate: it will not initiate the SSL handshake while a read or write is still in flight. `onReadMessage` and `onWriteMessage` each call `tryAsyncShutdown()` on completion when `shutdown_` is set, so the last in-flight operation always triggers the graceful termination. The 5-second `shutdownTimerInterval` safety timer in `onTimer` calls `close()` directly if the graceful SSL path stalls, preventing hanging connections. All these callbacks run on `strand_`, so no additional mutex is needed for flag access.

## Resource Accounting

`ChargeWithContext` is a small inner struct that accumulates the *highest* resource charge incurred while processing a batch of incoming messages. Its `update()` method asserts monotonic growth — the charge can only escalate, never decrease — reflecting a "worst case per message batch" policy. The accumulated fee is eventually applied to `usage_` (a `Resource::Consumer`) via `charge()`. This batching avoids per-fragment charge calls in the inner read loop, concentrating cost attribution at message boundaries.

The `Metrics` inner class tracks per-direction bandwidth with a 30-bucket circular rolling average (one bucket per second) using its own `shared_mutex` for thread-safe read access from diagnostic paths while writes happen on the strand. The `average_bytes()` accessor exposes current throughput for `json()` diagnostics and peer scoring.

## Locking Discipline

The code contains a candid 2019 audit comment acknowledging that locking evolved haphazardly. The practical rules are:

- **`recentLock_`** (plain `mutex`) guards `closedLedgerHash_`, `previousLedgerHash_`, `minLedger_`, `maxLedger_`, `recentLedgers_`, `recentTxSets_`, `trackingTime_`, `latency_`, `publisherListSequences_`, and `last_status_`. The `addLedger()` helper takes the lock by value (`std::lock_guard const&`) as a deliberate API forcing callers to hold the lock before calling — a statically-enforced contract.
- **`nameMutex_`** (a `shared_mutex`) guards `name_`, allowing concurrent reads from multiple threads while writes are exclusive.
- All I/O-related state (`shutdown_`, `shutdownStarted_`, `readPending_`, `writePending_`, `send_queue_`, `txQueue_`) is protected entirely by the strand — no mutex needed because they are only touched inside strand-dispatched callbacks.

## Transaction Reduce-Relay

When `txReduceRelayEnabled_` is true, `PeerImp` operates in a gossip-pull mode: instead of forwarding full transactions, the node collects unsent transaction hashes into `txQueue_` (capped at `reduce_relay::MAX_TX_QUEUE_SIZE`) and flushes them as batched `TMHaveTransactions` messages once per second via `sendTxQueue()`. The peer receiving this advertisement requests any hashes it hasn't seen. This asymmetry — advertise hashes, pull bodies on demand — drastically reduces duplicate full-transaction traffic across the mesh without sacrificing reliability.

## Relationship to `OverlayImpl`

`PeerImp` is declared `friend class OverlayImpl`, reflecting that the overlay manager needs to access internals for slot cleanup, traffic reporting via `reportOutboundTraffic()`, and squelch callbacks. The `Squelch<UptimeClock>` member (`squelch_`) is per-peer state for the validator-proposal reduce-relay protocol: it tracks which validators are currently squelched on this specific link, independent of the overlay-wide squelch table in `OverlayImpl`.