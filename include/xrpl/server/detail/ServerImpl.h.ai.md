# `include/xrpl/server/detail/ServerImpl.h`

## Role in the System

This file defines the multi-protocol network server at the heart of XRPL's node-facing communication layer. It provides two things: the `Server` abstract base class, which is the stable public interface callers hold onto, and the `ServerImpl<Handler>` template, which is the concrete implementation. Together they govern how the rippled process binds to TCP ports, accepts inbound connections, and dispatches them across HTTP, HTTPS, WebSocket, Secure WebSocket, and the peer-to-peer gossip protocol.

The public entry point is in the sibling `Server.h`, which exposes a single factory function:

```cpp
template <class Handler>
std::unique_ptr<Server> make_Server(Handler& handler, boost::asio::io_context& io_context, beast::Journal journal);
```

This factory wraps `ServerImpl<Handler>` behind the `Server` interface, giving callers a type-erased handle that survives across compilation units without exposing the Handler template.

## The `Server` Abstract Interface

`Server` is intentionally minimal — three pure virtual methods (`journal()`, `ports()`, `close()`) and a blocking virtual destructor. The sparse surface area is deliberate: callers configure ports once, then either wait for a graceful close or destroy the object. The interface hides all template machinery so that code outside the server subsystem never needs to know the `Handler` type.

## `ServerImpl<Handler>`: Ownership and Lifecycle

`ServerImpl` holds three key resources by reference, not by value:

- `handler_` — the application-layer callback object. The server sends events to it but does not own or manage its lifetime.
- `io_context_` — the Asio event loop driving all I/O. The server's existence must be a strict sub-interval of the `io_context`'s lifetime.
- `work_` — an `executor_work_guard` that prevents the `io_context` from returning from `run()` while the server is active. This is `std::optional` specifically so it can be released without destroying the object.

The strand `strand_` is constructed but not heavily used in this file; it lives here to be passed to components that need a serialized executor context.

## `ports()`: One-Shot Port Setup

`ports()` is a one-shot call that configures all listening endpoints. For each `Port` configuration entry it calls `ios_.emplace<Door<Handler>>(...)`, which atomically creates the listener and registers it with the lifecycle tracker. If the port was configured with `port = 0` (OS-assigned), the actual bound port is back-filled into the stored `Port` struct after `get_endpoint()` resolves it. The method returns an `Endpoints` map (port name → `tcp::endpoint`) so the caller can learn the actual addresses — critical when ephemeral ports are used in tests or when binding to port 0.

Calling `ports()` on a closed server throws `std::logic_error` rather than silently doing nothing. This enforces the single-setup contract.

## `close()`: Asynchronous Graceful Shutdown

`close()` initiates an asynchronous teardown by calling `ios_.close()` with a finisher lambda. The `io_list` machinery fans out `close()` calls to every registered `Door` (and transitively, every live connection), then invokes the finisher only after all work objects have been destroyed. The finisher lambda does two things in order:

1. Sets `work_ = std::nullopt` — releases the `executor_work_guard`, allowing the `io_context` to drain.
2. Calls `handler_.onStopped(*this)` — notifies the application layer that the server is fully stopped.

This sequencing is the non-obvious design choice: `onStopped` fires *after* all connection teardown, guaranteeing the handler sees a quiescent state when it receives the callback.

## Destructor: Silent Teardown

The destructor takes a different path than `close()`. The comment `// Handler::onStopped will not be called` is load-bearing documentation. It drops `work_`, calls `ios_.close()` (with a no-op finisher), and then synchronously blocks on `ios_.join()`. This is safe because the destructor must not return until all async operations that hold pointers into the server have completed, but it deliberately skips the application callback to prevent double-notification if `close()` was already called.

## `io_list` and the Lifetime Graph

`io_list` is the cohesion point for async lifetime management. Every object that performs I/O — `Door<Handler>`, `Detector`, `PlainHTTPPeer<Handler>`, `SSLHTTPPeer<Handler>` — derives from `io_list::work` and is created through `ios_.emplace<>()`. The `io_list` tracks all live work objects in a flat map of `work* → weak_ptr<work>`. When `close()` fires, it takes a snapshot of the map, releases the lock, and calls `close()` on every object via its `weak_ptr`. The reference count system ensures that if a connection completes naturally before the shutdown signal arrives, its destructor removes it from the count, and the finisher fires when `n_` reaches zero — whether that's before or after the close call.

The `io_list ios_` is exposed via the public `ios()` accessor so that `Door` and its inner `Detector` class can register new peer objects directly into the same lifetime graph, not into a separate one.

## `Door<Handler>`: Per-Port Accept Loop

Each `Door` wraps a single `tcp::acceptor` and runs a coroutine (`do_accept`) that loops calling `async_accept`. Two layers of backpressure are implemented:

- **Proactive**: Before each accept, `should_throttle_for_fds()` queries `/proc/self/fd` (Linux) or `/dev/fd` (BSD/macOS) and compares used file descriptors against the process `RLIMIT_NOFILE`. If more than 30% of the FD budget is consumed (i.e., free ratio drops below `FREE_FD_THRESHOLD = 0.70`), the accept loop backs off before even attempting an accept.
- **Reactive**: On `EMFILE` or `ENOBUFS` errors from the OS, a backoff timer fires with exponential growth from 50 ms to a ceiling of 2000 ms, resetting to the initial value on any successful accept.

Protocol dispatch happens at the `Door` level. If a port has both SSL and plain protocols enabled (`ssl_ && plain_`), a `Detector` object is spawned. `Detector` peeks at the first 16 bytes of the stream with a 15-second timeout using `boost::beast::async_detect_ssl`, then routes to `SSLHTTPPeer` or `PlainHTTPPeer` accordingly. If only one protocol family is configured, `create()` skips detection and dispatches directly.

## Residual Members

The `hist_` array of 64 `size_t` values, the `high_` counter, and the private `ceil_log2()` static method are declared but never referenced in active code. They appear to be scaffolding left from an earlier connection-count histogram feature that was removed or never completed. They add no behavior and impose negligible overhead.