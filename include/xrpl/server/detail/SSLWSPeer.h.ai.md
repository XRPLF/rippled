# `SSLWSPeer.h` — TLS WebSocket Connection Peer

## Role in the System

`SSLWSPeer` is the concrete leaf class for TLS-encrypted WebSocket connections in the XRPL server. It sits at the bottom of a CRTP inheritance stack that separates transport-agnostic WebSocket logic from transport-specific wiring. Its sole structural responsibility is to own and expose the correctly layered TLS stream so that the base classes can drive all I/O through it without needing to know whether the underlying transport is encrypted or not.

## Inheritance and CRTP Design

The class inherits from `BaseWSPeer<Handler, SSLWSPeer<Handler>>`, which in turn inherits from `BasePeer<Handler, SSLWSPeer<Handler>>`. Both base classes are parameterized with the concrete derived type and use `static_cast<Impl*>(this)` (via the protected `impl()` helper) to access members of the derived class — the classic Curiously Recurring Template Pattern.

This design avoids virtual dispatch for every I/O operation while still sharing the full lifecycle, write queue, ping/pong heartbeat, and read loop logic in `BaseWSPeer`. The tradeoff is that `BasePeer` and `BaseWSPeer` are both declared as `friend` classes of `SSLWSPeer`, since they reach directly into its private `ws_` and `stream_ptr_` members.

`std::enable_shared_from_this<SSLWSPeer<Handler>>` is mixed in directly on the concrete class rather than on a base, which is intentional: `shared_from_this()` must return a pointer to the most-derived type so that the shared ownership count is correctly tied to the object's actual lifetime and so that `impl().shared_from_this()` in the base classes resolves to the right type.

## Stream Layer Architecture

The TLS WebSocket stack has three layers, all held within `SSLWSPeer`:

```
boost::beast::websocket::stream<stream_type&>   ws_         (WebSocket framing)
boost::beast::ssl_stream<socket_type>            *stream_ptr_ (TLS encryption)
boost::beast::tcp_stream                         (underlying TCP)
```

`stream_ptr_` is a `std::unique_ptr<stream_type>`, and `ws_` is declared as `boost::beast::websocket::stream<stream_type&>` — a websocket stream layered over a *reference* to the SSL stream, not over an owned value. This is the key structural difference from `PlainWSPeer`, where `ws_` owns its socket directly via `boost::beast::websocket::stream<socket_type>`. The reference-wrapper approach is necessary because the SSL stream is heap-allocated and its address must remain stable; embedding it by value inside the websocket stream would make move semantics unsafe.

## Constructor and Initialization Order

The constructor receives a `std::unique_ptr<stream_type>&&` — an already-constructed and TLS-handshaked stream transferred in from `SSLHTTPPeer::websocketUpgrade()`. The initialization order is load-bearing:

```cpp
: BaseWSPeer<Handler, SSLWSPeer>(
      port,
      handler,
      stream_ptr->get_executor(),          // extract executor BEFORE move
      waitable_timer{stream_ptr->get_executor()},  // extract executor BEFORE move
      remote_endpoint,
      std::move(request),
      journal)
, stream_ptr_(std::move(stream_ptr))       // take ownership
, ws_(*stream_ptr_)                        // reference the now-owned object
```

The executor is extracted from `stream_ptr` (the parameter, still valid) before ownership is transferred. C++ guarantees that `BaseWSPeer` is initialized before `stream_ptr_`, which is initialized before `ws_`. So by the time `ws_(*stream_ptr_)` runs, `stream_ptr_` already holds the heap-allocated SSL stream. If the order were reversed — `ws_` initialized before `stream_ptr_` — `ws_` would hold a dangling reference.

## Lifecycle and Upgrade Path

`SSLWSPeer` instances are never constructed directly by application code. The creation path is:

1. `Door` accepts an incoming TCP connection and detects TLS via Beast's `async_detect_ssl`.
2. An `SSLHTTPPeer<Handler>` is created to perform the TLS handshake and read the HTTP Upgrade request.
3. When `SSLHTTPPeer::do_request()` determines the request is a WebSocket upgrade, it calls `websocketUpgrade()`, which constructs an `SSLWSPeer` by moving the already-authenticated TLS stream and the HTTP request into it.
4. `BaseWSPeer::run()` is then called to initiate the WebSocket handshake over the existing TLS connection.

This upgrade path means `SSLWSPeer` never needs to manage TLS negotiation itself — it receives a fully-established TLS session and lifts it into WebSocket framing.

## Relationship to Sibling Classes

The four concrete peer types — `PlainHTTPPeer`, `SSLHTTPPeer`, `PlainWSPeer`, and `SSLWSPeer` — form a 2×2 matrix of transport (plain/TLS) by protocol (HTTP/WebSocket). `SSLWSPeer` is structurally symmetric to `PlainWSPeer` except for the extra indirection through `stream_ptr_` and the reference-based `ws_` type. The entire behavioral surface — reading, writing, heartbeating, queue management, and connection teardown — lives in `BaseWSPeer` and is shared between both WebSocket variants.