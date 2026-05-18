# `include/xrpl/server/Handoff.h`

## Role in the System

`Handoff.h` defines the `Handoff` struct — a lightweight result type that encodes the outcome of a server's connection handoff decision. It is the return value of `onHandoff()` callbacks throughout the XRPL server layer, and it also establishes the canonical type aliases `http_request_type` and `http_response_type` (both wrapping Boost.Beast's `dynamic_body` variants) that are used across the entire server module.

The handoff concept exists because the XRPL server must classify each inbound HTTP connection at the moment a request arrives: is it a regular RPC call, a WebSocket upgrade, a peer-protocol connection, or a status probe? Each of these paths has a fundamentally different lifetime model for the underlying socket and response, so a single return value needs to express all of those outcomes cleanly to the dispatch layer.

## The `Handoff` Struct

The struct carries three fields that together encode which of three possible outcomes the handler has chosen:

**`moved` (bool, default `false`)**: Signals that the handler has taken ownership of the underlying socket via `std::move`. When this is `true`, the `Session` layer must relinquish the socket and stop touching it — the handler is now responsible for its lifetime. This path is taken, for example, when `ServerHandler::onHandoff()` detects a WebSocket upgrade request and hands the socket off to a `PlainWSPeer` or `SSLWSPeer`, or when it routes a peer-protocol connection to the Overlay subsystem.

**`response` (`std::shared_ptr<Writer>`, default null)**: When non-null, the session should serialize and send this response back to the client rather than forwarding the request to higher-level RPC processing. The `Writer` abstraction (from `Writer.h`) provides an async-friendly pull-style interface with `prepare()`, `consume()`, and `data()` methods, so the response can be streamed in chunks without blocking the I/O thread.

**`keep_alive` (bool, default `false`)**: Meaningful only when `response` is set. It tells the session layer whether to close the TCP connection after the response is flushed or to linger waiting for the next request. This mirrors the HTTP/1.1 `Connection: keep-alive` semantics.

## The `handled()` Predicate

```cpp
bool handled() const { return moved || response; }
```

This one-liner is the discriminated-union test: a `Handoff` is considered "handled" if the connection was taken over (`moved`) or if there is an inline response to send. A zero-value `Handoff{}` — both fields false and `response` null — signals to the dispatch layer that the handler did not recognize or claim the request, allowing the session to fall through to legacy `onRequest()` processing.

## Dispatch Logic in Context

In `PlainHTTPPeer<Handler>::do_request()`, the dispatcher calls `handler_.onHandoff()` and then reads the returned `Handoff` in a concise three-way branch:

```cpp
auto const what = this->handler_.onHandoff(...);
if (what.moved)  return;                          // socket taken, do nothing
if (what.response) {                              // send inline response
    if (!what.keep_alive) socket_.shutdown(...);
    return this->write(what.response, what.keep_alive);
}
// else: fall through to handler_.onRequest()
```

This pattern avoids callbacks, virtual dispatch on the result type, or exceptions for routing decisions. The value is cheap (two bools and a `shared_ptr`) and its semantics are self-documenting at each call site.

## How Producers Construct `Handoff` Values

`ServerHandler::onHandoff()` is the primary producer. It examines the incoming request and returns:

- `Handoff{.moved = true}` for WebSocket and peer-protocol upgrades, where ownership of the socket transfers to another subsystem.
- `Handoff{.response = writer, .keep_alive = ...}` for status requests (HTTP 200/500 load-balancing probes) and other self-contained HTTP responses that don't require the full RPC pipeline.
- A default `Handoff{}` (`handled() == false`) when none of these early-exit paths match, which causes the connection to proceed to normal RPC request processing via `onRequest()`.

The `OverlayImpl` similarly uses `Handoff` when its own `onHandoff()` is called to service peer HTTP endpoints (`/crawl`, `/vl/<key>`, `/health`). Each of these `process*()` helpers populates `handoff.response` with a `SimpleWriter` wrapping an HTTP response and returns `true`, causing the caller to return the filled `Handoff` immediately.

## Design Rationale

The key non-obvious choice is representing the outcome as a value type rather than an output parameter, a virtual method, or a `std::variant`. Because the struct is trivially constructible and holds only a `shared_ptr` plus two bools, returning it by value is zero-overhead relative to the I/O work that follows. The `handled()` predicate provides a single boolean gate that works regardless of which specific outcome was chosen, which is useful for early-exit tests in tests and in the overlay dispatch loop. The fact that `moved` and a non-null `response` are logically mutually exclusive is enforced by convention at the call sites, not by the type system — a deliberate trade-off for simplicity given the small number of producers.