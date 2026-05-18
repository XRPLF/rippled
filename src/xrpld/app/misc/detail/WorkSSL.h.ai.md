# `WorkSSL.h` — TLS-secured HTTP Work Unit

## Role in the System

`WorkSSL` is the TLS-layer specialization of the XRPL node's outbound HTTP client machinery. It lives alongside `WorkPlain` as one of two concrete implementations of `Work`, the abstract interface for "a single asynchronous HTTP GET request." The broader mechanism is used by the validator list fetcher, amendment voting subsystem, and other services that need to pull data from external HTTPS endpoints (typically `vl.ripple.com` and similar trusted sources).

## CRTP Inheritance Chain

The design uses the Curiously Recurring Template Pattern to share connection and I/O logic without virtual dispatch overhead. `Work` (in `Work.h`) provides a minimal interface: `run()` and `cancel()`. `WorkBase<Impl>` is a template that handles DNS resolution, TCP connection, HTTP request construction, and HTTP response reading — the complete request lifecycle except for the stream type and the post-TCP-connect handshake. The template calls back into `Impl` through two customization points: `impl().stream()` to get the I/O stream to read/write, and `impl().onConnect(ec)` to perform any transport-layer setup once the TCP socket is established.

`WorkSSL` and `WorkPlain` are the two `Impl` specializations. `WorkPlain::onConnect` trivially calls `onStart()` immediately; `WorkSSL::onConnect` must first run the TLS handshake. The friend declaration `friend class WorkBase<WorkSSL>` gives the base template access to the private `onConnect` and `stream` members — the CRTP coupling is deliberate and contained.

## TLS Stream Ownership Model

A key detail in the member layout:

```cpp
HTTPClientSSLContext context_;
stream_type stream_;  // boost::asio::ssl::stream<socket_type&>
```

`stream_type` wraps a *reference* to `socket_` (owned by `WorkBase`), not a copy. This is mandatory because Asio's SSL stream takes ownership of async operations on the underlying socket through the reference, while the raw TCP socket object must remain in scope for its lifetime. By wrapping `socket_&`, `WorkSSL` layered the TLS state on top without transferring socket ownership.

## Two-Phase SSL Verification

The OpenSSL/Asio interface imposes a temporal ordering constraint that forces SSL setup to be split across two points in the connection lifecycle. `WorkSSL` handles this explicitly:

**Constructor (before TCP connect):** `context_.preConnectVerify(stream_, host_)` calls `SSL_set_tlsext_host_name` to configure the TLS SNI extension. SNI tells the server which certificate to present during the handshake and *must* be set before the TCP connection is initiated. If this fails (an OpenSSL error), the constructor throws `std::runtime_error` immediately — there is no valid state to recover to.

**`onConnect` (after TCP connect, before TLS handshake):** `context_.postConnectVerify(stream_, host_)` installs the peer verification mode and callback. If `SSL_VERIFY` is enabled in config, it sets `verify_peer` mode and registers `rfc6125_verify` as the certificate check callback, which delegates to Asio's `host_name_verification` (RFC 6125 hostname matching) and logs a warning on failure. This cannot be done earlier because the SSL stream's verify settings require the stream to be in a connected state.

Only after both checks pass does the async TLS handshake begin. `onHandshake` then calls `onStart()` to transmit the HTTP request over the now-encrypted channel.

## Configuration Surface

`HTTPClientSSLContext` is constructed from three config values: `SSL_VERIFY_DIR`, `SSL_VERIFY_FILE`, and `SSL_VERIFY`. The TLS protocol is fixed to `tlsv12_client` — hardcoded in `WorkSSL.cpp`, not exposed as a parameter. This is a deliberate minimum-version floor for outbound connections rather than a negotiable option. If a verify file is provided, the system certificate store is bypassed entirely; if a verify directory is provided, it is added as an additional search path on top of the default store.

## Lifetime and Concurrency Safety

All async callbacks are bound to a `strand_` (inherited from `WorkBase`) and capture `shared_from_this()`, which returns a `shared_ptr<WorkSSL>` (via `std::enable_shared_from_this<WorkSSL>`). This guarantees the object outlives all pending async operations and that callbacks never race with each other even when the underlying `io_context` runs on a thread pool. The `callback_type` (a `std::function` holding the caller's completion handler) is nulled out after first invocation to prevent double-delivery — critical because the destructor of `WorkBase` fires a synthetic error callback if one is still pending.

## Relationship to `WorkPlain`

`WorkPlain` is structurally identical but omits `HTTPClientSSLContext` and `stream_type` entirely — its `stream()` method returns `socket_` directly. The symmetry makes the two classes useful for understanding each other: every complexity in `WorkSSL` relative to `WorkPlain` corresponds to something SSL requires that plain TCP does not.