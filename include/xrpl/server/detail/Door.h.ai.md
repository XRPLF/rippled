# `Door.h` — TCP Listening Socket with Protocol Detection and FD-Aware Throttling

## Role in the System

`Door<Handler>` is the outermost entry point for all inbound TCP connections in the XRPL server. Each configured `Port` (HTTP, HTTPS, WebSocket, WSS, peer protocol) gets one `Door` instance. Its job is to own a `boost::asio::ip::tcp::acceptor`, run a continuous accept loop, and hand accepted sockets off to the appropriate peer type — `PlainHTTPPeer` or `SSLHTTPPeer`. Everything downstream (request parsing, WebSocket upgrades, handler dispatch) is owned by those peer objects; `Door` is purely the front door.

The class is a header-only template parameterized on `Handler`, the application-level callback type that implements `onAccept`, `onHandoff`, and `onRequest`. This templating avoids virtual dispatch on the hot path while keeping the networking layer decoupled from business logic.

## Lifetime and the `io_list` Contract

`Door` inherits from both `io_list::work` and `std::enable_shared_from_this`. The `io_list` is a reference-counted work registry: when `io_list::close()` is called it dispatches `close()` on every registered `work` item, and the destructor blocks until all work is destroyed. This lets the server shut down cleanly without ad-hoc join loops.

Because `shared_from_this()` cannot be called inside a constructor, `Door` separates construction from activation: the constructor calls `reOpen()` to set up the acceptor (open, `SO_REUSEADDR`, bind, listen), while `run()` is called afterward to start the coroutine. The same split applies to `Detector`.

`close()` is thread-safe: if called off the `Door`'s strand it posts back to the strand before canceling the backoff timer and closing the acceptor. Closing the acceptor causes the `async_accept` in progress to complete with `operation_aborted`, which breaks the accept loop.

## The Accept Loop and Protocol Dispatch

`do_accept` runs as a Boost.Asio stackful coroutine (`yield_context`). It loops while the acceptor is open, calling `async_accept` to obtain a raw `boost::beast::tcp_stream` and remote endpoint.

After a successful accept, the loop checks the `ssl_` and `plain_` flags, which are computed once at construction from the `Port::protocol` set:

```cpp
bool ssl_{
    port_.protocol.count("https") > 0 || port_.protocol.count("wss") > 0 ||
    port_.protocol.count("wss2") > 0 || port_.protocol.count("peer") > 0};
bool plain_{
    port_.protocol.count("http") > 0 || port_.protocol.count("ws") > 0 ||
    port_.protocol.count("ws2")};
```

If only one protocol family is configured (the common case), `create()` is called directly with the correct boolean — no sniffing overhead. If both SSL and plain are configured on the same port, a `Detector` is spawned to peek at the first bytes.

## The `Detector` Inner Class

`Detector` is itself an `io_list::work`, registered in the same `io_list` as `Door`. It owns the stream during the detection window. `do_detect` calls `boost::beast::async_detect_ssl`, which reads up to 16 bytes into a `multi_buffer` with a 15-second timeout. The buffer is passed directly into the `SSLHTTPPeer` or `PlainHTTPPeer` constructor, so those bytes are not lost — they become the head of the first read. This zero-copy design avoids any re-injection mechanism.

If detection errors with anything other than `operation_aborted` (which is the normal shutdown path), only a trace-level log is emitted and the socket is silently dropped. This is intentional: a malformed or slow opener should not pollute error logs.

## Exponential Backoff on Accept Errors

`do_accept` implements two-tier backoff for resource exhaustion:

**Reactive**: If `async_accept` returns `no_descriptors` (EMFILE) or `no_buffer_space` (ENOBUFS), the loop pauses on `backoff_timer_` and doubles `accept_delay_`, capped at `MAX_ACCEPT_DELAY` (2000 ms). After a successful accept, `accept_delay_` resets to `INITIAL_ACCEPT_DELAY` (50 ms).

**Proactive (non-Windows)**: Before each `async_accept`, `should_throttle_for_fds()` checks whether free file descriptors have fallen below 30% of the process limit. It reads the FD count by enumerating `/proc/self/fd` (Linux) or `/dev/fd` (other POSIX) via `opendir`/`readdir`, subtracting 3 for `.`, `..`, and the `DIR*` itself. If the `FREE_FD_THRESHOLD` (0.70) is breached, the loop backs off before even attempting an accept, avoiding the EMFILE error entirely.

The proactive path is silently skipped on Windows (returns `false`) and whenever `getrlimit` returns `RLIM_INFINITY` or fails — both cases where the check is meaningless or impossible.

This two-layer design matters because EMFILE recovery on Linux is lossy: the kernel may queue further connections during the pause, but the OS backlog is finite. Catching the condition before it turns into an error gives the server a chance to drain existing connections before new ones arrive.

## Thread Safety

All mutable `Door` state is protected by a `strand_`. The `Detector` has its own independent strand. `close()` is explicitly strand-aware and posts to its strand if called from a foreign thread. The `io_list` mutex serializes `emplace` and `work` destruction. There is no shared mutable state between `Door`, `Detector`, and the peer types except the `io_list` itself.

## Relationship to Peer Types

`Door` is the only place where `PlainHTTPPeer` and `SSLHTTPPeer` are constructed. Once `create()` or `Detector::do_detect` calls `sp->run()`, `Door` has no further reference to the peer — the `io_list` holds the only tracked pointer. This clean ownership boundary means peer teardown is driven entirely by I/O completion and `io_list` shutdown, not by `Door`.