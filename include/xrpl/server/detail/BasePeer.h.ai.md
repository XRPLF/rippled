# `BasePeer.h` — Shared Foundation for WebSocket Peer Connections

`BasePeer` is a compact, two-template-parameter mixin that captures the state and strand-safety logic shared by every WebSocket peer connection in the XRPL server. It sits one level below `BaseWSPeer` in the inheritance chain, providing the five fields and the thread-safe `close()` implementation that all concrete WebSocket peers — `PlainWSPeer` and `SSLWSPeer` — depend on.

## Role in the Peer Hierarchy

The XRPL server's connection type hierarchy splits at the protocol level. HTTP connections use `BaseHTTPPeer` (which independently owns its own strand and port reference), while WebSocket connections use `BaseWSPeer`, which in turn derives from `BasePeer`. This file is therefore the root of the WebSocket peer family only.

`BasePeer<Handler, Impl>` itself inherits from `io_list::work`, plugging every peer into the server's lifetime management system (described below). `BaseWSPeer<Handler, Impl>` then extends `BasePeer` and also inherits `WSSession`, adding the full WebSocket read/write/ping/timer machinery.

## The CRTP `impl()` Pattern

Both template parameters carry specific responsibilities. `Handler` is the application-level callback sink — typically the object that processes XRPL protocol messages via `onWSMessage()`. `Impl` is the fully-resolved concrete class (`PlainWSPeer` or `SSLWSPeer`), and the class exploits **CRTP** (Curiously Recurring Template Pattern) through the private `impl()` accessor:

```cpp
Impl& impl() { return *static_cast<Impl*>(this); }
```

This allows `BasePeer::close()` to reach into `impl().ws_` — the concrete WebSocket stream object — without a virtual dispatch. The alternative of a virtual getter for the stream would add indirection on the hot path and complicate the layering of SSL vs. plain streams.

## Lifetime Management via `io_list::work`

`io_list` is the server's registry of active asynchronous work objects. Every peer registers itself via `io_list::emplace()`, which atomically inserts a `shared_ptr` into the registry. When the server shuts down, `io_list::close()` iterates all registered work and calls the virtual `close()` on each, then blocks in `join()` until every work object's reference count reaches zero.

`BasePeer::close()` is the implementation of that virtual `close()`. The registration/deregistration cycle is entirely automatic: `io_list::work::destroy()` (called from the destructor) removes the peer from the registry and fires the completion finisher if the count reaches zero.

## Strand Enforcement in `close()`

```cpp
void BasePeer<Handler, Impl>::close()
{
    if (!strand_.running_in_this_thread())
        return post(strand_, std::bind(&BasePeer::close, impl().shared_from_this()));
    error_code ec;
    xrpl::get_lowest_layer(impl().ws_).socket().close(ec);
}
```

This pattern — check the strand, re-post if not running on it, then act — appears throughout the entire peer hierarchy. The strand serializes all async operations belonging to a single peer. Since `io_list::close()` calls peer `close()` from arbitrary threads, the re-post ensures the actual socket teardown happens in the correct execution context. Ignoring the error code on the `socket().close(ec)` call is intentional: by the time `close()` is invoked, the peer is being torn down regardless of whether the close operation itself fails.

`get_lowest_layer()` in `LowestLayer.h` abstracts a Boost version incompatibility: before Boost 1.70, accessing the underlying TCP socket required `t.lowest_layer()`; afterward, it uses `boost::beast::get_lowest_layer(t)`.

## Per-Connection Log Identity

The constructor generates a globally unique, monotonically increasing connection ID using a local `static std::atomic<unsigned>`:

```cpp
sink_(journal.sink(), [] {
    static std::atomic<unsigned> id{0};
    return "##" + std::to_string(++id) + " ";
}())
```

The `beast::WrappedSink` prefixes every log message from this peer with the ID string (e.g., `"##42 "`). The `j_` journal is then constructed from this wrapped sink, so all logging through `this->j_` in derived classes is automatically tagged. This makes it straightforward to correlate log lines from the same connection across reads, writes, and timeouts.

## `executor_work_guard` and Executor Lifetime

`work_` holds a `boost::asio::executor_work_guard` for the peer's executor. This prevents the `io_context` from running out of work and stopping while the peer still has pending async operations. The guard is held for the full lifetime of the `BasePeer` object and released when the peer is destroyed.

## Relationship to `BaseHTTPPeer`

Notably, `BaseHTTPPeer` does not derive from `BasePeer` — it re-declares its own `port_`, `handler_`, `work_`, `strand_`, and `remote_address_` fields independently. The two hierarchies share the same conceptual structure but were implemented separately, reflecting that HTTP and WebSocket peers have sufficiently different lifecycles (e.g., HTTP supports keep-alive and detach) that sharing a base class would require careful abstraction. `BasePeer` is exclusively for the WebSocket branch.