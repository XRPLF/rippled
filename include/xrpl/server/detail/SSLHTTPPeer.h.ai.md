# `SSLHTTPPeer.h` — TLS-Wrapped HTTP Connection Peer

## Role in the System

`SSLHTTPPeer` is one of the four concrete connection types in the XRPL server's peer hierarchy, sitting alongside `PlainHTTPPeer`, `SSLWSPeer`, and `PlainWSPeer`. It handles the full lifecycle of an inbound TLS-encrypted HTTP connection: SSL handshake negotiation, HTTP request reading, handler dispatch, optional in-place upgrade to a WebSocket session, and graceful TLS shutdown. Every `https`, `wss`, `wss2`, and `peer` protocol connection in the rippled server goes through this class.

`Door`, the listening socket abstraction in the same `detail/` directory, creates instances of this class. When `Door`'s internal `Detector` detects SSL bytes at the front of the stream (using `boost::beast::async_detect_ssl`), it instantiates an `SSLHTTPPeer` and calls `run()` to kick off the connection state machine.

## Class Structure and Ownership

```cpp
template <class Handler>
class SSLHTTPPeer : public BaseHTTPPeer<Handler, SSLHTTPPeer<Handler>>,
                    public std::enable_shared_from_this<SSLHTTPPeer<Handler>>
```

The class uses the **Curiously Recurring Template Pattern (CRTP)** through `BaseHTTPPeer<Handler, SSLHTTPPeer<Handler>>`. `BaseHTTPPeer` is a template that accepts the concrete implementation type as its second parameter, which lets the base class call derived-class methods like `stream_` and `shared_from_this()` without virtual dispatch overhead. The `friend class BaseHTTPPeer<Handler, SSLHTTPPeer>` declaration is necessary for this access.

The stream type hierarchy is three layers deep:

- `socket_type` → raw `boost::asio::ip::tcp::socket`
- `middle_type` → `boost::beast::tcp_stream` (adds Beast's timeout support around the socket)
- `stream_type` → `boost::beast::ssl_stream<middle_type>` (wraps with TLS via Asio's SSL layer)

The `stream_ptr_` is a `std::unique_ptr<stream_type>`, while `stream_` is a direct reference into it and `socket_` is a reference into `stream_.next_layer().socket()`. This split ownership-vs-reference pattern is intentional: `stream_ptr_` can be **moved out** of `SSLHTTPPeer` during a WebSocket upgrade (transferring TLS stream ownership to `SSLWSPeer`), while local operations throughout the peer's lifetime use the cheaper references. After the move, `stream_` and `socket_` would dangle, but that only happens at the point the peer is destroyed anyway.

## Connection Lifecycle

**Construction** receives a `middle_type&&` (an already-accepted `tcp_stream`) and a `Port` reference. The `Port::context` member supplies the `boost::asio::ssl::context` used to construct the `ssl_stream`. Any data bytes the `Detector` already consumed from the socket (its pre-read buffer) are passed as `buffers` and committed into `BaseHTTPPeer`'s `read_buf_` for replay during the handshake.

**`run()`** is the entry point after acceptance. It first calls `handler_.onAccept()` to allow the application to reject the connection by IP address or other policy before any TLS is negotiated — an important early-exit gate. If the handler accepts, it spawns a coroutine to execute `do_handshake()`.

**`do_handshake()`** performs the TLS server handshake asynchronously using `stream_.async_handshake()`, passing `read_buf_.data()` so the SSL library can consume those pre-peeked bytes. `verify_mode` is set to `ssl::verify_none` because XRPL clients are not expected to present certificates — mutual TLS is not used here. After the handshake, the method checks `port().protocol` to determine whether the connection should continue reading HTTP (`https`, `wss`, `wss2`, `peer`). If none of these protocols are configured, it simply returns and lets `this` be destroyed — a purposely silent drop used for protocol-sniffing scenarios where the connection is handled elsewhere.

**`do_request()`** is called by `BaseHTTPPeer::do_read()` after a full HTTP message has been parsed. It increments the request counter and calls `handler_.onHandoff()`, passing the fully-negotiated `stream_ptr_` by move. If the handler signals `what.moved`, ownership has been transferred and this peer returns immediately. If the handler provides a `what.response`, it is written back. Otherwise, the legacy `onRequest()` path is invoked. Notably, SSL connections do **not** perform the half-close `socket_.shutdown(shutdown_receive)` that `PlainHTTPPeer` does for non-keep-alive connections — TLS shutdown is bidirectional and handled separately.

**`websocketUpgrade()`** transitions an HTTP connection to a WebSocket session by constructing an `SSLWSPeer` via `ios().emplace<>()`, moving the HTTP message and `stream_ptr_` into it. This is a zero-copy handoff of the TLS stream; the new `SSLWSPeer` wraps it in a `boost::beast::websocket::stream<stream_type&>` for the WebSocket framing layer. The contrasting plain variant, `PlainHTTPPeer::websocketUpgrade()`, moves just a `tcp_stream` value (no pointer indirection), because it does not need the pointer trick for detachability.

## Shutdown and Error Handling

**`do_close()`** initiates a TLS `async_shutdown` with the timer running, then dispatches to `on_shutdown()` on the strand. TLS shutdown is an active protocol exchange — both sides must send `close_notify` alerts — so unlike a plain TCP close it cannot be done synchronously. The timer guards against a peer that never sends `close_notify`.

**`on_shutdown()`** cancels the timer and explicitly calls `stream_.next_layer().close()` on the underlying `tcp_stream`. The comment "in case this->destructor is delayed" reflects that `SSLHTTPPeer` is ref-counted: there may be outstanding callbacks holding `shared_ptr` copies that extend the object's lifetime past shutdown. Closing the socket at `on_shutdown()` time ensures the OS-level resource is released immediately rather than waiting for the last reference to drop.

Error handling throughout follows the same pattern as `BaseHTTPPeer`: errors are stored in `ec_` and the lowest-layer stream is closed immediately, allowing all pending async operations to drain to their completion handlers with `operation_aborted`.

## Concurrency

All async operations are dispatched through `strand_` (inherited from `BaseHTTPPeer`), serializing all state mutations without explicit locking. The mutex in `BaseHTTPPeer` is limited to the write queue (`wq_`/`wq2_`), which may be accessed from non-strand threads via the `write(void*, size_t)` path when the application layer pushes data from an arbitrary thread. `SSLHTTPPeer` itself adds no new shared mutable state beyond `stream_ptr_`, which is only ever moved once, under strand protection, during `do_request()` or `websocketUpgrade()`.