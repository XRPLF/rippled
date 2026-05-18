# PeerImp.cpp

## Role and Purpose

`PeerImp` is the concrete, single-connection implementation of XRPL's peer-to-peer overlay. Each instance represents one live TLS-over-TCP session with a remote rippled node. The class sits at the intersection of three concerns: asynchronous I/O lifecycle, the XRPL binary wire protocol, and overlay-level resource accounting. Everything that touches an active peer — reading messages, sending messages, answering ledger queries, relaying validations and proposals, enforcing rate limits, and shutting down cleanly — is centralized here.

---

## Construction and Feature Negotiation

The constructor takes an already-established TLS stream, the peer's verified `PublicKey`, the negotiated `ProtocolVersion`, and HTTP upgrade headers. Three optional capabilities are negotiated at construction via `peerFeatureEnabled`: **LZ4 compression**, **TX reduce-relay**, and **ledger replay**. These flags are stored permanently and characterise the session's capability set. A `fingerprint_` derived from the remote endpoint, public key, and numeric ID prefixes every log line from the two journals (`journal_` for connection events, `p_journal_` for protocol events).

---

## Strand-Based Thread Safety

All mutable state is serialized through a single Boost.Asio `strand_`. Every public method self-posts to the strand if not already running there. A separate `recentLock_` mutex protects the fields that diagnostics and consensus code read from outside the strand (ledger hashes, sequence ranges, latency, tracking time).

---

## Shutdown State Machine

Shutdown follows four stages coordinated by `shutdown_` and `shutdownStarted_` flags. `shutdown()` sets the flag and cancels I/O; `tryAsyncShutdown()` defers the SSL close until no reads or writes are in flight; `onShutdown()` handles the SSL completion; `close()` closes the raw socket. A 5-second safety timer forces `close()` if the SSL teardown hangs.

---

## Message Dispatch and Resource Charging

`onReadMessage` drives the read loop, calling `invokeProtocolMessage` repeatedly to dispatch to typed `onMessage` overloads. `onMessageBegin`/`onMessageEnd` bracket each dispatch: the former resets a `ChargeWithContext fee_` accumulator, the latter applies the final charge via `charge()`. Individual handlers call `fee_.update()` to escalate the charge tier for malformed, duplicated, or expensive data. If a peer's resource balance reaches the drop threshold, it is disconnected immediately.

---

## Key Design Patterns Across Handlers

- **`stringIsUint256Sized` guard**: Called before every raw-byte-to-`uint256` conversion to prevent silent misuse of protobuf `string` fields.
- **Duplicate suppression**: `HashRouter::addSuppressionPeer` is checked before processing proposals, validations, and validator lists, preventing relay storms.
- **Job queue dispatch**: All cryptographic and CPU-heavy work (signature verification, transaction application, ledger assembly) is posted to the job queue rather than blocking the network strand. Job lambdas capture the peer as `weak_ptr` and silently no-op if the peer was destroyed.
- **TX reduce-relay**: Hash announcements (`TMHaveTransactions`) replace full transaction flooding when the feature is negotiated. `addTxQueue`/`sendTxQueue` batch hashes into per-peer sets; the peer's `handleHaveTransactions` requests only the missing ones.
- **Latency measurement**: A random 32-bit cookie in each `TMPing` prevents spoofed pong responses. RTT is smoothed with an 8-factor EWMA.
- **Peer tracking**: The `Tracking` atomic enum (`unknown` / `converged` / `diverged`) is updated by comparing the peer's reported ledger sequence against the locally validated index. Outbound peers that remain non-converged past configurable timeouts are disconnected as "Not useful".