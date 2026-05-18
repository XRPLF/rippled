# `PlainHTTPPeer.h` — Non-TLS HTTP Peer Connection

`PlainHTTPPeer` is the concrete HTTP connection type for unencrypted (non-TLS) TCP clients. It sits at the leaf of a three-tier class hierarchy: the template base `BaseHTTPPeer` provides all async I/O machinery, `PlainHTTPPeer` supplies the plain-socket specialisation, and the parallel `SSLHTTPPeer` provides the TLS variant. Together they handle everything from connection acceptance through request dispatch, optional WebSocket upgrade, and graceful shutdown.

## Inheritance and CRTP Design

```
BaseHTTPPeer<Handler, PlainHTTPPeer<Handler>>   ← CRTP base (async loops, write queue, timers)
   └── PlainHTTPPeer<Handler>                   ← plain-TCP specialisation
          └── enable_shared_from_this
```

`BaseHTTPPeer` uses the Curiously Recurring Template Pattern: it holds a reference to the concrete subtype via `impl()` and calls `impl().stream_` directly in methods like `boost::beast::get_lowest_layer(impl().stream_).close()` and in `do_read` / `on_write`. This avoids a virtual dispatch hot-path in the I/O loop while still allowing `do_request()` and `do_close()` to be pure-virtual overrides. `PlainHTTPPeer` is declared a `friend` of its own base so the base can access the private `stream_` member.

## Stream Representation

The class owns a `boost::beast::tcp_stream` (`stream_`), which wraps a raw TCP socket with Beast's built-in per-operation deadline support. A plain `boost::asio::ip::tcp::socket&` (`socket_`) is stored as a reference into `stream_.socket()`. This reference exists because some operations — socket options, half-close via `shutdown()` — require the raw socket interface that Beast's stream adaptor doesn't expose. The two members always refer to the same underlying file descriptor; `socket_` is never a separate object.

## Constructor: TCP_NODELAY on Loopback

```cpp
if (remote_endpoint.address().is_loopback())
    socket_.set_option(boost::asio::ip::tcp::no_delay{true});
```

This is an intentional test-infrastructure optimisation. Nagle's algorithm buffers small writes in hope of coalescing them, which noticeably inflates round-trip latency in the `Env`-based integration test suite running on Linux. On real network paths the coalescing is usually desirable, so the option is only applied for loopback. The base class constructor also differentiates loopback clients with a shorter `timeoutSecondsLocal` (3 s vs. 30 s) for the same reason.

## `run()`: Acceptance Gate

`run()` is called by `Door` after a connection is accepted. It immediately calls `handler_.onAccept()` to give the application layer a chance to reject the connection (e.g., connection-limit enforcement). If the handler returns false, the peer schedules `do_close()` and exits. Because `onAccept` may itself close the socket, a second check on `socket_.is_open()` guards against double-close before posting `do_read`.

All coroutine launches go through `util::spawn` with an explicit `strand_` to serialise I/O operations on a single logical thread of execution, even when the underlying `io_context` runs on a thread pool.

## `do_request()`: Three-Way Dispatch

After `BaseHTTPPeer::do_read` parses a complete HTTP message it calls the pure-virtual `do_request()`. The plain-HTTP implementation offers three dispatch paths:

1. **Handler handoff** (`what.moved == true`): `onHandoff` takes ownership of the connection — used when upgrading to a peer overlay connection or similar out-of-band routing. The peer does nothing further.
2. **Immediate response** (`what.response != nullptr`): The handler synthesised a ready-made response (e.g., a redirect or 403). If `Connection: close` was requested, the receive side is half-closed before writing. The response is then queued via `BaseHTTPPeer::write`.
3. **Legacy `onRequest`**: For JSON-RPC and other application-layer handlers that pull the request from the `Session` interface and call `session.write()` themselves.

The explicit half-close in paths 1 and 2 — `socket_.shutdown(shutdown_receive)` — signals to the remote that no further requests will be read. This is the correct TCP idiom for HTTP/1.1 `Connection: close` on a plain socket. The TLS counterpart (`SSLHTTPPeer::do_request`) omits this step because TLS shutdown requires an async bidirectional `close_notify` handshake handled separately in `do_close()`.

## `do_close()`: Half-Close for Send

```cpp
void PlainHTTPPeer<Handler>::do_close() {
    boost::system::error_code ec;
    socket_.shutdown(socket_type::shutdown_send, ec);
}
```

For plain TCP, terminating the outbound direction with `shutdown_send` is sufficient. The remote will read EOF, complete any pending read, and then close its end. The TLS peer instead needs `stream_.async_shutdown()` to exchange `close_notify` alerts, which is why `do_close` is virtual. The error code from `shutdown` is intentionally discarded; by this point the peer's lifetime is already ending.

## `websocketUpgrade()`: Protocol Handoff

When `onHandoff` returns a WebSocket upgrade signal (detected from HTTP headers by the base layer), `websocketUpgrade()` is called. It constructs a `PlainWSPeer` via `ios().emplace<>()`, moving both the `stream_` and the already-parsed HTTP upgrade `message_` into the new peer. The move semantics are critical: after this call `stream_` is in a valid-but-empty state and the `PlainHTTPPeer` must not perform any further I/O. The returned `WSSession` shared pointer is registered with the `io_list` and takes over the connection's lifetime.

## Relationship to `Door` and `SSLHTTPPeer`

`Door` accepts raw TCP connections and uses `boost::beast::async_detect_ssl` to sniff the first bytes. If the connection looks like TLS it creates an `SSLHTTPPeer`; otherwise it creates a `PlainHTTPPeer`. Both types accept the same `ConstBufferSequence buffers` argument (the bytes already read during SSL detection) which `BaseHTTPPeer` replays into `read_buf_` so no data is lost. This sniffing-plus-prepend design allows a single port to serve both plain and TLS clients without the client needing to connect to different ports.