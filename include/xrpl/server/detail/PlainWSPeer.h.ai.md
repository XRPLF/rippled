# `PlainWSPeer.h` — Plain (Non-TLS) WebSocket Peer

`PlainWSPeer` is the concrete WebSocket peer implementation for unencrypted connections in the XRPL server. It sits at the leaf of a three-level inheritance hierarchy: `BasePeer → BaseWSPeer → PlainWSPeer`, and serves as the exact structural mirror of `SSLWSPeer` — the only difference being that it wraps a raw `boost::beast::tcp_stream` rather than an SSL-layered one.

## Role in the Peer Hierarchy

The server's peer design uses the Curiously Recurring Template Pattern (CRTP): `BaseWSPeer<Handler, Impl>` and `BasePeer<Handler, Impl>` both cast `this` down to `Impl*` whenever they need to touch the transport-specific `ws_` stream. This lets both base classes drive all async I/O — read loops, write queues, ping/pong timers, close sequences — without virtual dispatch, while remaining completely unaware of whether the underlying stream is plain TCP or TLS.

`PlainWSPeer` satisfies that contract by holding:

```cpp
boost::beast::websocket::stream<socket_type> ws_;
```

where `socket_type` is `boost::beast::tcp_stream`. The member is `private`, accessible only to the two base classes via explicit `friend` declarations. `BaseWSPeer` directly calls `impl().ws_.async_read(...)`, `impl().ws_.async_write_some(...)`, `impl().ws_.async_close(...)`, and so on — all resolved statically.

## Constructor Design

The constructor receives an already-HTTP-upgraded request, the raw TCP socket (by move), and the other standard peer parameters. Two ordering constraints matter here. First, `socket.get_executor()` must be extracted *before* the socket is moved, because the base class needs the executor to create its strand and timer — both of which are initialized in `BasePeer` and `BaseWSPeer` respectively. Second, `ws_` is initialized after the base, ensuring the base is fully constructed before the WebSocket stream exists.

Compare this to `SSLWSPeer`, where the SSL stream is held in a `unique_ptr<stream_type>` and the WebSocket stream is `websocket::stream<stream_type&>` — a reference wrapper. That extra indirection is needed because `ssl_stream` is not moveable after construction. The plain variant has no such constraint: `tcp_stream` is moveable, so `ws_` can own the socket directly without heap allocation.

## Lifetime and Thread Safety

`PlainWSPeer` inherits `std::enable_shared_from_this`, which is the mechanism by which every async callback captures a `shared_ptr` to itself. This prevents the peer from being destroyed while an operation is outstanding. The `BasePeer` constructor also holds a `boost::asio::executor_work_guard`, keeping the ASIO executor alive for the duration of the peer's existence. All public methods on `BaseWSPeer` (`send`, `close`, `complete`) guard against cross-thread access by re-posting to the strand when not already on it — `PlainWSPeer` inherits this safety without adding any locking of its own.