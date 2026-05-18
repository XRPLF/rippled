# `ConnectAttempt.h` — Outbound Peer Connection State Machine

`ConnectAttempt` encapsulates the complete lifecycle of one outbound TCP/TLS/HTTP connection attempt to a remote XRPL peer. It lives inside the `xrpld/overlay/detail/` subsystem and is instantiated by `OverlayImpl` whenever the PeerFinder decides a new outbound connection should be made. Once the handshake succeeds, ownership of the underlying stream is transferred to a newly constructed `PeerImp` and the `ConnectAttempt` object dissolves. On any failure it simply shuts down and the PeerFinder slot is released from the destructor.

## Inheritance and Ownership

The class inherits from two bases. `OverlayImpl::Child` registers it with the overlay's child-tracking table, ensuring that `stop()` will be called on every live attempt when the overlay shuts down — a straightforward plugin pattern that avoids a separate registry. `std::enable_shared_from_this<ConnectAttempt>` is required because every async callback captures a `shared_ptr` to the attempt via `shared_from_this()`. This guarantees the object stays alive for the duration of any in-flight operation, which is the standard idiom for safely using Boost.Asio with reference-counted objects.

## Dual-Timer Design

The most architecturally interesting feature is the two-timer scheme. A single global `timer_` fires after `connectTimeout` (25 seconds) and acts as the hard limit for the entire operation. A second `stepTimer_` is reset at each phase transition with a much tighter bound: 8 s for TCP connect, 8 s for TLS handshake, 3 s for the HTTP write, 3 s for the HTTP read, and 2 s for SSL shutdown. Both timers share a single `onTimer()` handler that distinguishes them by comparing their expiry times to `steady_clock::now()`.

The reason to keep two timers rather than just one is diagnostic precision. A slow TLS handshake that doesn't exceed 25 seconds total would be invisible with only a global timeout; the step timer surfaces exactly which phase is hanging and logs it. The global timer is initialized lazily — `setTimer()` checks whether `timer_.expiry()` is still at the epoch before arming it — so it is set exactly once for the lifetime of the attempt regardless of how many phase transitions occur.

## Connection Pipeline

`run()` is the entry point. It arms `ConnectionStep::TcpConnect` and calls `async_connect` on the underlying TCP socket. Each completion handler (`onConnect`, `onHandshake`, `onWrite`, `onRead`) follows an identical pattern:

1. Clear `ioPending_`.
2. Check for `operation_aborted` (timer cancellation during shutdown) and route to `tryAsyncShutdown()`.
3. Check for any other error and call `fail()`.
4. Check the `shutdown_` flag.
5. Set `ioPending_ = true`, arm the next step timer, and issue the next async operation.

The `ioPending_` flag is the key to safe shutdown sequencing. `tryAsyncShutdown()` will not start the SSL shutdown handshake until `ioPending_` is false, because starting an async SSL shutdown while another async operation is pending on the same stream would be undefined behaviour. Instead it defers, and the next completion handler calls `tryAsyncShutdown()` when it clears the flag. This avoids a race without any extra locks.

## TLS Without Verification

A subtle policy decision in `onConnect` is `stream_.set_verify_mode(boost::asio::ssl::verify_none)`. The peer identity is not validated through the TLS certificate chain; instead, both sides derive a `sharedValue` from the TLS session's finished data via `makeSharedValue()`, then cryptographically bind their node public keys to that value in the HTTP headers using `buildHandshake()`. This is a MITM-resistant proof-of-possession scheme that doesn't require a PKI: if a man-in-the-middle breaks the TLS session, the finished-data values on each side will differ, and the handshake will fail even though TLS certificate verification was skipped.

## `processResponse()` — The Critical Path

After HTTP headers are read, `processResponse()` handles three outcomes. If the peer responded with `101 Switching Protocols`, it validates the negotiated XRPL protocol version, recomputes the shared value, calls `verifyHandshake()` to authenticate the remote node's public key, and calls `PeerFinder::activate()` to officially register the peer. On success it constructs a `PeerImp`, moves the stream pointer and the `PeerFinder::Slot` into it (nulling `slot_` in the process — the destructor checks for this to avoid a double-close), and hands the peer to `OverlayImpl::add_active()`.

If the peer returns `503 Service Unavailable` with a JSON body containing a `"peer-ips"` array, it is treated as a redirect: the IPs are parsed via the inline `parse_endpoint()` helper and forwarded to `PeerFinder::onRedirects()` so the discovery layer can connect to the suggested peers. Any other response code is treated as a hard failure.

## Thread Safety

All mutable state is accessed only on the `strand_`. Both `run()` and `stop()` begin with a guard that posts to the strand if the call arrives from outside it, so callers don't need to worry about synchronization. This makes the class safe to stop from any thread, which is necessary because `OverlayImpl::stopChildren()` may be called from the main thread while the strand runs on an IO thread pool.

## Resource Cleanup

If `ConnectAttempt` is destroyed before a successful connection (error, timeout, or external stop), the destructor calls `overlay_.peerFinder().on_closed(slot_)` to free the PeerFinder slot. When a `PeerImp` is created, `slot_` is moved into it, leaving the local member null, so the destructor's guard correctly skips the release. This single ownership pattern ensures every slot is closed exactly once.