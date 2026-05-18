# `include/xrpl/server/WSSession.h` — WebSocket Message and Session Interfaces

## Role in the System

This header defines the two core abstractions that drive all WebSocket I/O in the XRPL server: `WSMsg`, the interface for a sendable WebSocket message, and `WSSession`, the interface for an active WebSocket connection. Together they form the boundary between the application layer (the RPC/subscription handler) and the network transport layer (`BaseWSPeer` and its TLS/plain concrete implementations).

The file sits alongside `Session.h`, which covers HTTP. `Session::websocketUpgrade()` returns a `shared_ptr<WSSession>`, making the two interfaces the two branches of a single connection lifecycle: HTTP for one-shot request/response, WebSocket for persistent bidirectional streams.

## `WSMsg` — Lazy, Chunked Message Abstraction

`WSMsg` is a non-copyable abstract base with a single pure-virtual method:

```cpp
virtual std::pair<boost::tribool, std::vector<boost::asio::const_buffer>>
prepare(std::size_t bytes, std::function<void(void)> resume) = 0;
```

The `boost::tribool` return value carries the framing signal:

- `indeterminate` (`maybe`) — data is not ready; the implementation will call `resume` later when it is.
- `false` — a partial chunk is ready; the caller should write it and call `prepare` again.
- `true` — the final chunk is ready; after this write the message is complete.

This three-state protocol elegantly handles both synchronous and asynchronous data sources with one interface. An implementation backed by a live database cursor can return `indeterminate` and post the `resume` callback when the next page arrives, whereas a fully buffered implementation can always return `true` on the first call. Neither the transport layer (`BaseWSPeer`) nor the message sender needs to know which case applies.

`BaseWSPeer::on_write()` drives this protocol directly, calling `prepare(65536, ...)` in a loop and dispatching either `async_write_some` (non-final) or `async_write_some` (final, triggering `on_write_fin`). The `resume` functor it passes is a bound call to `BaseWSPeer::do_write()`, which re-enters the write loop — so an async-ready `WSMsg` simply holds onto the functor and invokes it from its own callback.

### `StreambufWSMsg<Streambuf>` — The Common Case

The only concrete implementation in this header wraps any Boost.Asio `Streambuf` (in practice, `boost::beast::multi_buffer`). It maintains a cursor `n_` tracking how many bytes were handed out in the previous call and uses `sb_.consume(n_)` at the start of each `prepare()` call to advance the buffer. Because the buffer is already fully populated at construction time, the `resume` callback is unused (ignored by the unnamed parameter) and the method always returns synchronously: `false` when the remaining data exceeds `bytes`, `true` when it fits or the buffer is empty. This is the path taken for JSON-encoded ledger events and RPC replies that are serialized before queuing.

## `WSSession` — Abstract Connection Handle

`WSSession` is a pure-virtual, non-copyable struct representing one live WebSocket connection. Its interface is deliberately minimal:

```cpp
virtual void run() = 0;
virtual Port const& port() const = 0;
virtual http_request_type const& request() const = 0;
virtual boost::asio::ip::tcp::endpoint const& remote_endpoint() const = 0;
virtual void send(std::shared_ptr<WSMsg>) = 0;
virtual void close() = 0;
virtual void close(boost::beast::websocket::close_reason const&) = 0;
virtual void complete() = 0;
```

`run()` initiates the Beast WebSocket handshake from the already-upgraded TCP/TLS stream. `send()` enqueues a `WSMsg` for asynchronous delivery; the concrete implementation in `BaseWSPeer` enforces `Port::ws_queue_limit` — if the queue depth exceeds the configured ceiling, it flushes the backlog, sets a policy-error close reason, and initiates a close, protecting the server from slow or stalled clients.

`complete()` is the session's backpressure gate. After each incoming WebSocket frame, `BaseWSPeer::on_read()` calls `handler_.onWSMessage(...)` and then waits. The handler must call `complete()` to trigger `do_read()` again, ensuring the server never buffers more than one inbound message per connection at a time. This prevents runaway memory growth from fast-talking clients.

The one data member, `std::shared_ptr<void> appDefined`, is a type-erased application attachment point. The XRPL handler layer stores per-session subscription state here without requiring `WSSession` to know anything about the subscription model. Because it is a `shared_ptr`, the attached object is safely destroyed when the session is torn down, even if the handler still holds a reference to the `WSSession` itself.

## Design Decisions Worth Noting

**`WSMsg` vs `Writer`**: The HTTP counterpart `Writer` uses a classic pull model with separate `prepare()`, `data()`, and `consume()` calls. `WSMsg` collapses this into a single `prepare()` returning buffer and completion state together. WebSocket messages are inherently framed, so the transport layer already knows the message boundary; the simplified interface maps naturally onto `async_write_some` with the `fin` flag.

**Strand enforcement in `BaseWSPeer`**: Every virtual method on `WSSession` is implemented with an upfront strand check (`if (!strand_.running_in_this_thread()) return post(...)`). This means callers may invoke `send()` or `close()` from any thread — a common requirement when subscription notifications arrive on ledger-processing threads — without additional synchronization at the call site.

**Close ordering**: `BaseWSPeer::close(reason)` sets `do_close_ = true` but, if the write queue is non-empty, records the reason in `cr_` and defers the actual `ws_.async_close()` until `on_write_fin()` drains the queue. This guarantees that buffered outbound messages are fully delivered before the close frame is sent, satisfying the RFC 6455 requirement to complete pending sends before initiating closure.