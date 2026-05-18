# `include/xrpl/server/Session.h`

## Role in the System

`Session` is the abstract interface representing a single live HTTP connection in the XRPL server stack. Every TCP or TLS connection accepted by the server is surfaced to application-level handlers exclusively through this interface, isolating the handler from transport specifics (plain vs. SSL, buffer management, Asio strand serialization). The concrete implementation lives in `detail/BaseHTTPPeer.h`, which is a `CRTP`-like class template parameterized on both the `Handler` type and the transport type (`PlainHTTPPeer` / `SSLHTTPPeer`). Application code — chiefly `ServerHandler` — never touches those templates directly; it operates only against `Session&`.

## Key Design Decisions

### Pure-virtual interface with a `void* tag` escape hatch

The interface is non-copyable and fully abstract: every method that must vary by transport is pure virtual. The one exception is the `void* tag` member. This is a deliberate, low-overhead extension point that lets the handler attach arbitrary per-connection state (an associated `WSInfoSub`, a request counter, a decoded auth token, etc.) without forcing `Session` to grow application-specific fields or requiring a `dynamic_cast`. The implementation guarantees `tag` is zero-initialized at construction and persisted between callbacks for the connection's lifetime — a classic C-style slot that trades type safety for zero coupling.

### `write()` overload family

Three overloads service the write path. The two concrete convenience methods — one for `std::string`, one for a generic `BufferSequence` — both bottom out in the single pure virtual `write(void const* buffer, std::size_t bytes)`. This reduces the implementation surface to one method while providing ergonomic call sites. The second pure virtual overload, `write(std::shared_ptr<Writer> const& writer, bool keep_alive)`, handles streaming or lazily-materialized response bodies: `Writer` exposes a `prepare()/consume()` pull model that allows the session to apply I/O backpressure. The `keep_alive` flag is passed here rather than inferred from the request because the handler may override it based on application policy.

### `detach()` for asynchronous response generation

XRPL's RPC system dispatches request processing onto a job queue that runs on worker threads outside of the Asio `io_context`. Once the handler submits the job, it must keep the session alive until the worker eventually calls `complete()`. `detach()` solves this by returning a `shared_ptr<Session>` that the handler holds — extending the object's lifetime beyond the server's own reference. Critically, the docstring notes that `io_context::run()` will not return while any detached session remains open, which means detached sessions participate in the graceful shutdown protocol. This is the central async lifecycle mechanism for HTTP sessions.

### Two-phase `write()` + `complete()` model

Following HTTP/1.1 semantics, a handler writes response data incrementally and then calls `complete()` to signal the end of the response. `complete()` does not close the connection; instead, it triggers keep-alive cycling (issuing a new read for the next request) if the connection negotiated keep-alive, or a graceful shutdown otherwise. This two-phase model is what allows connection reuse across multiple request/response cycles.

### `close(bool graceful)` for explicit teardown

`close()` is asynchronous — it schedules closure after all pending write operations drain. The `graceful` flag controls whether the close waits for the send buffer to flush or aborts immediately. This is important for error paths (e.g., malformed request, auth failure) where the server wants to send a 4xx response and then close, versus administrative shutdown where it may want to drop immediately.

### `websocketUpgrade()` — protocol transition

When a handler determines an incoming HTTP request is a WebSocket upgrade (by inspecting `request()`), it calls `websocketUpgrade()` to hand off the socket to a `WSSession`. The returned `shared_ptr<WSSession>` owns the connection going forward; the `Session` is no longer valid for writes after this call. `WSSession` is a sibling interface (defined in `WSSession.h`) with its own `send(shared_ptr<WSMsg>)` and `complete()` semantics suited to the WebSocket framing model. This clean protocol-switch design means the HTTP and WebSocket paths share the socket's physical connection but use entirely separate message-framing and lifecycle APIs.

## Relationship to `Port`, `Writer`, and `WSSession`

`Session::port()` returns a `const Port&` describing the server listener that accepted this connection — its protocol set, SSL context, admin IP ranges, WebSocket queue limits, and credentials. This lets a single `Handler` implementation distinguish how to treat a request based on which port it arrived on (e.g., an admin-only port vs. a public JSON-RPC port) without needing separate handler instances.

`Writer` (from `Writer.h`) is the pull-based streaming abstraction used in the `write(shared_ptr<Writer>, bool)` overload. It models an input sequence that the session drains in chunks, calling `prepare()` to fill and `consume()` to advance, stopping when `complete()` returns `true`. This lets response generators (like those producing large ledger data) avoid materializing the entire body in memory.

`WSSession` (from `WSSession.h`) shares the `appDefined` void pointer slot under a different name but serves the same purpose as `Session::tag` — handler-defined per-connection state. Both interfaces also share `complete()` semantics, reinforcing that the notify-when-done pattern is a server-wide contract rather than HTTP-specific.