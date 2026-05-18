# `ConnectAttempt.cpp` — Outbound Peer Connection Lifecycle Manager

`ConnectAttempt` is the dialer for the XRPL overlay network. When the `PeerFinder` subsystem decides the node needs more connections, `OverlayImpl` creates a `ConnectAttempt` to drive the entire process of reaching a remote peer — from raw TCP socket through TLS encryption and an HTTP-based capability handshake — and either promoting the result into a live `PeerImp` or reporting failure back to `PeerFinder`. This file contains the implementation of that state machine.

## Connection Pipeline

The connection follows a strict five-phase sequence managed by async callbacks chained through Boost.Asio:

1. `run()` → `async_connect` → `onConnect()`
2. `onConnect()` → `async_handshake` (TLS client) → `onHandshake()`
3. `onHandshake()` → `async_write` (HTTP upgrade request) → `onWrite()`
4. `onWrite()` → `async_read` (HTTP upgrade response) → `onRead()`
5. `onRead()` → `processResponse()` → `PeerImp` construction or failure

Each callback in the chain checks for errors, a pending shutdown flag, and socket validity before proceeding. If any check fails, it routes to `shutdown()`. This strict guard-and-delegate structure keeps each callback lean and the error paths uniform.

## Dual-Timer Design

The most architecturally distinctive feature is the two-timer system. A **global timer** (`timer_`) is set once at the start of `run()` with a 25-second hard ceiling. A **step timer** (`stepTimer_`) is reset at the start of each phase with tighter, phase-specific limits: 8 seconds each for TCP connect and TLS handshake, 3 seconds each for the HTTP write and read, and 2 seconds for TLS shutdown.

Both timers share the single `onTimer()` callback. Because Boost.Asio will call the handler with `operation_aborted` when a timer is cancelled, the callback simply calls `std::chrono::steady_clock::now()` and compares it against each timer's expiry to determine which one fired. This avoids the need for separate handler functions or tagging parameters, at the cost of one additional time comparison.

The global timer is only ever started once — guarded by checking that its expiry equals the default-constructed `time_point{}`. Subsequent calls to `setTimer()` only update the step timer, which automatically cancels the previous step timer via `expires_after`. This means the global timer ticks continuously across all phases while each phase has its own tighter budget, giving good diagnostic granularity in logs without needing two separate timeout mechanisms per phase.

## Thread Safety: Strand Enforcement

All operations are serialized on `strand_`, a `boost::asio::strand` constructed from the `io_context`. The two public entry points, `run()` and `stop()`, both contain a guard that re-posts to the strand if called from outside it, making them safe to call from any thread. The private `shutdown()` and `tryAsyncShutdown()` methods assert via `XRPL_ASSERT` that they are always running on the strand — a fast-fail for programming errors.

## `ioPending_` and Safe Shutdown Interleaving

A boolean `ioPending_` tracks whether an async I/O operation is in flight. `tryAsyncShutdown()` is a no-op when `ioPending_` is true; it only proceeds once the outstanding operation completes and calls back. Each async callback clears `ioPending_` first, then checks the `shutdown_` flag and calls `tryAsyncShutdown()` if needed. This prevents calling `stream_.async_shutdown()` while a `beast::http::async_write` or `async_read` is still pending on the same stream, which would be undefined behavior.

## Selective TLS Shutdown

`tryAsyncShutdown()` only initiates an SSL `async_shutdown` if the TLS handshake has been completed. If the connection is torn down during `TcpConnect` or `TlsHandshake` phases, there is no TLS session to close, so `close()` is called directly to shut the socket. This distinction prevents calling `async_shutdown` on a stream that never completed its handshake, which can cause spurious errors.

`onShutdown()` deliberately suppresses logging for several expected error codes: `eof` (clean closure), `operation_aborted` (timeout-driven cancellation), `stream_truncated` (peer closed without a handshake), and "application data after close notify" (a benign SSL condition that some peers trigger). Only genuinely unexpected errors are logged.

## Ownership Transfer via `slot_` Nullability

The `slot_` member (a `std::shared_ptr<PeerFinder::Slot>`) encodes whether the connection succeeded. On destruction, the destructor checks `if (slot_ != nullptr)` and calls `peerFinder().on_closed(slot_)`. This cleanup is essential — every slot opened in `PeerFinder` must eventually be closed or the finder's bookkeeping becomes inconsistent.

When `processResponse()` successfully creates a `PeerImp`, it moves `slot_` into the new peer object. The `ConnectAttempt` then holds a null `slot_`, so its destructor skips the `on_closed` call. The `stream_ptr_` unique pointer is likewise moved into `PeerImp`, leaving the `ConnectAttempt` without ownership of the socket, which is correct because the object is about to be destroyed.

## `processResponse()`: The Handshake Verification Core

This is the most security-sensitive method. A well-behaved peer returns HTTP 101 (Switching Protocols) with an `Upgrade` header listing its supported protocol versions. The code extracts these, verifies that exactly one is both present and supported locally via `isProtocolSupported()`, and rejects any ambiguous or unsupported negotiation.

Before accepting the connection, `verifyHandshake()` validates the peer's claimed identity using the TLS-derived shared value computed by `makeSharedValue()`. This shared value is derived from the TLS session's finished messages, ensuring that if a man-in-the-middle is present the two sides will compute different values and the verification will fail. If the peer is a cluster member, that is noted in the log and used by `peerFinder().activate()`.

A separate branch handles a `503 Service Unavailable` response carrying a JSON body with a `peer-ips` array. This is the XRPL redirect mechanism: an overloaded peer that cannot accept a new connection provides a list of alternative addresses. `ConnectAttempt` extracts and validates these endpoints and forwards them to `overlay_.peerFinder().onRedirects()`, where they feed back into the peer discovery pool. Non-redirect 503 responses (e.g., from a plain HTTP server) are rejected with a warning log rather than treated as redirect candidates.

## Relationship to `PeerImp` and `OverlayImpl`

`ConnectAttempt` inherits from `OverlayImpl::Child`, a minimal interface that requires implementing `stop()`. `OverlayImpl` holds a weak pointer to every child and calls `stop()` on all of them during overlay shutdown. Once the connection is promoted, `overlay_.add_active(peer)` registers the new `PeerImp` and the `ConnectAttempt` falls out of scope, causing its destructor to run (slot already null, so no cleanup needed on the `PeerFinder` side). The `PeerImp` then owns all subsequent I/O on that connection.