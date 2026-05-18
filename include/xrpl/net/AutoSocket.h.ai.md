# `AutoSocket.h` — Protocol-Agnostic TCP Socket Wrapper

`AutoSocket` exists to solve a practical deployment problem: XRPL nodes must accept incoming connections that may be either TLS-encrypted or plaintext without requiring the remote peer to declare its protocol upfront. Rather than running two separate listening ports or mandating coordinated configuration between peers, `AutoSocket` wraps a single `boost::asio::ssl::stream<tcp::socket>` and defers the SSL/plain decision to connection time, with an optional byte-sniffing auto-detection path. Its only known consumer in the codebase is `HTTPClient.cpp`, where it provides the transport layer for outbound HTTP/HTTPS client requests.

## Core Design: Always Allocate the SSL Socket

The central structural decision is that the `ssl_socket` — a `boost::asio::ssl::stream<boost::asio::ip::tcp::socket>` — is always heap-allocated via `mSocket` (`std::unique_ptr<ssl_socket>`), regardless of whether the connection ultimately turns out to be plaintext. The plain TCP socket is the `next_layer_type` of the SSL stream, so it is always present as an embedded sub-object. There is no variant type, no polymorphic pointer hierarchy, and no conditional allocation based on mode. The `mSecure` boolean records the final protocol determination, and every subsequent I/O method (`async_read`, `async_write`, `async_read_some`, etc.) simply branches on it to dispatch to either `*mSocket` (SSL path) or `PlainSocket()` (the raw `next_layer()`).

This is deliberately simple. Once the handshake phase resolves the mode, the socket behaves uniformly for its remaining lifetime. The cost is that every connection — even definitely-plain ones — carries the overhead of a full `ssl_socket` allocation.

## Three Construction Modes

The primary constructor accepts `secureOnly` and `plainOnly` flags that control both `mSecure` and the size of `mBuffer`:

- **`secureOnly = true`**: `mSecure` is initialized to `true`, `mBuffer` is empty (size 0). SSL is assumed immediately; no byte-sniffing occurs.
- **`plainOnly = true`**: `mSecure` is left `false`, `mBuffer` is empty (size 0). The plaintext path is assumed immediately; no peek occurs.
- **Both false (default)**: `mSecure` starts `false`, `mBuffer` is allocated to 4 bytes. Auto-detection will be performed during `async_handshake`.

The convenience constructor `AutoSocket(io_context&, ssl::context&)` delegates to the primary with both flags false, enabling auto-detection.

## The `async_handshake` Decision Tree

`async_handshake` is the central branching point, and its logic encodes all three modes:

1. **Client role or already-secure**: If `type == ssl_socket::client` or `mSecure` is already `true`, SSL is set unconditionally and `mSocket->async_handshake` is called directly. Clients always know whether they intend to use TLS.

2. **Empty buffer (forced-plain or forced-secure construction)**: A zero-size `mBuffer` means either `plainOnly` or `secureOnly` was set. In the plain case, `mSecure` is `false` and the handler is posted immediately with a success `error_code` via `boost::beast::bind_handler`. This lets callers call `async_handshake` uniformly without knowing the mode, and they will always receive an asynchronous callback.

3. **Auto-detect**: A 4-byte buffer is present. The code issues an `async_receive` on the plain socket layer with `message_peek` — this reads bytes from the kernel's receive buffer without consuming them, so they remain available for subsequent reads (including the TLS `ClientHello` parser if SSL is detected). The result is dispatched to `handle_autodetect`.

## The Auto-Detection Heuristic

`handle_autodetect` applies a byte-range test: if all received bytes fall in the printable ASCII range (32–126 inclusive), the connection is classified as plaintext. If any byte falls outside that range, SSL is assumed and `mSocket->async_handshake(ssl_socket::server, cbFunc)` is issued.

The heuristic is reliable in practice because TLS `ClientHello` records begin with byte `0x16` (22), which is a control character well below the printable range. Text-based protocols like HTTP begin with uppercase ASCII letters (`GET`, `POST`, `HTTP`), all in range. The 4-byte sample is large enough to distinguish these with high confidence while minimizing the peek overhead.

A subtle correctness point: since `message_peek` does not consume the bytes, the peeked data remains in the kernel buffer. When the TLS handshake reader subsequently processes the socket, it will see the full `ClientHello` starting from byte 0 — nothing is lost.

## Async Shutdown Asymmetry

`async_shutdown` reveals an important asymmetry between the two modes. TLS shutdown is a protocol-level exchange (`ssl::stream::async_shutdown`) and is genuinely asynchronous. TCP shutdown for plain sockets is synchronous and immediate (`shutdown_both`). Rather than expose two different calling conventions, the plain path performs the synchronous `shutdown_both`, catches any `boost::system::system_error` into an `error_code`, and then posts the handler to the executor using `bind_handler`. This preserves the invariant — critical for correctness in Boost.Asio code — that completion handlers are never called synchronously from within an initiating function call.

## Resource Management and Swap

`mSocket` is owned exclusively via `std::unique_ptr`, giving `AutoSocket` clear sole ownership with no shared state. `swap()` provides a no-throw exchange of all members (`mBuffer`, `mSocket`, `mSecure`), useful for connection-accepting code that needs to transfer socket ownership to a session object. There is no copy constructor or copy assignment, consistent with unique resource ownership.

The `j_` journal member is initialized to `beast::Journal::getNullSink()`, making all logging in `handle_autodetect` effectively a no-op by default. This reflects the class's age — it predates more systematic logger injection — and means the `JLOG` trace and warn calls in `handle_autodetect` are silent unless a caller explicitly wires in a real journal sink.

## Notable Limitations

There is no timeout on the auto-detection peek. A client that connects but sends nothing will hold the `async_receive` open indefinitely, preventing completion of `async_handshake`. Callers are responsible for imposing connection-level timeouts externally (e.g., via a `steady_timer`). Similarly, once `mSecure` is determined, there is no re-negotiation mechanism — the protocol mode is fixed for the socket's lifetime.