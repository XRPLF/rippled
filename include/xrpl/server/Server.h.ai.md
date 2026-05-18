# `include/xrpl/server/Server.h`

## Role and Purpose

`Server.h` is the public entry point for the XRPL multi-protocol HTTP server. The file itself is remarkably thin — just a single factory function — but it acts as the seam between callers and a deeply layered, template-heavy implementation. Its job is to let application code create a server without knowing the concrete type of the handler or the full implementation hierarchy.

The single exported function is:

```cpp
template <class Handler>
std::unique_ptr<Server>
make_Server(Handler& handler, boost::asio::io_context& io_context, beast::Journal journal)
```

The caller passes a handler by reference and receives an owning `unique_ptr<Server>` — an abstract base class. From that point on, the caller interacts with the server entirely through the narrow `Server` interface, which exposes only three operations: `ports()`, `close()`, and `journal()`.

## Why a Factory Function?

The key design decision here is the use of a non-member factory template rather than a public constructor or a class template. The handler is the customization point: `ServerImpl<Handler>` is a concrete type parameterized over the handler, but the caller should not need to name that type. By returning `unique_ptr<Server>`, the factory erases the handler type entirely. The handler itself is never owned — it is taken by reference — so the caller retains full control of its lifetime. This avoids the complications of shared ownership while still decoupling the server's template machinery from its users.

In practice, the only production caller is `ServerHandler` in `src/xrpld/rpc/detail/ServerHandler.cpp`, which passes `*this` as the handler during construction:

```cpp
m_server(make_Server(*this, io_context, app_.getJournal("Server")))
```

`ServerHandler` satisfies the Handler concept and handles all HTTP/WebSocket request dispatch for the node's RPC subsystem.

## The `Server` Abstract Interface

The `Server` class — defined inside `ServerImpl.h` and pulled in via the `#include` chain — is the interface the caller retains. Its design is intentionally sparse:

- `ports(std::vector<Port> const&)` configures the listening endpoints. It may only be called once; calling it on a closed server throws `std::logic_error`. It returns an `Endpoints` map (a `std::unordered_map<string, tcp::endpoint>`) so callers can discover the actual bound port numbers, which matters when port 0 is configured (OS-chosen ephemeral port).
- `close()` initiates asynchronous shutdown. The server is considered stopped when all pending I/O completions have drained and all connections have closed. Calling `handler_.onStopped(*this)` signals completion. The destructor blocks until this is achieved.
- `journal()` returns the logging journal — a pattern used throughout XRPL to allow scoped, categorized logging.

## `ServerImpl<Handler>`: The Concrete Implementation

`ServerImpl<Handler>` is the template class that actually does the work. Its internal structure is worth understanding:

- It holds a reference to `io_context_` and creates a `strand_` to serialize its own state changes. An `executor_work_guard` in `work_` keeps the `io_context` from exiting early while the server is live; releasing `work_` (by assigning `std::nullopt`) signals readiness to let the event loop finish.
- The `io_list ios_` member is the lifecycle manager for all active async objects. Every `Door` and every peer connection registers with this list. When `close()` is called, `io_list` propagates the close signal to all registered work items and invokes the finisher callback only after the last one has been destroyed. This avoids the classic race where a finisher fires before pending completions have run.
- The list of `Door<Handler>` objects is stored as `std::vector<std::weak_ptr<Door<Handler>>>`, not strong pointers. Ownership belongs to the `io_list`. The weak references are just for inspection; if a door has already been destroyed the weak pointer returns null.

## `Door<Handler>`: Listening Sockets and Protocol Detection

Each configured `Port` spawns a `Door<Handler>`. A `Door` is a Boost.Asio coroutine-based acceptor loop. On each accepted connection it must decide whether the connection is plain or TLS. If the port is configured for both (e.g., both `http` and `https` in its protocol set), `Door` creates a `Detector` — an inner class that reads up to 16 bytes with a 15-second timeout using `async_detect_ssl` from Boost.Beast. Based on the result, it instantiates either `PlainHTTPPeer<Handler>` or `SSLHTTPPeer<Handler>`, both of which are also `io_list::work` objects tracked by the same `io_list`.

If the port is exclusively plain or exclusively SSL, detection is skipped and the peer is created directly, saving a round-trip read.

`Door` also implements proactive file-descriptor throttling on POSIX systems. Before each accept it calls `should_throttle_for_fds()`, which reads `/proc/self/fd` (Linux) or `/dev/fd` (other POSIX) to count open file descriptors against the process `RLIMIT_NOFILE` limit. If fewer than 30% of file descriptors are free, the accept loop pauses for an exponentially increasing backoff starting at 50 ms and capping at 2 seconds. This prevents the server from hitting `EMFILE` under sustained connection load.

## `io_list`: Coordinated Async Shutdown

`io_list` is the linchpin of safe teardown. It maintains a `flat_map` of `work*` to `weak_ptr<work>`. Every `Door`, `Detector`, `PlainHTTPPeer`, and `SSLHTTPPeer` inherits `io_list::work` and registers on construction via `io_list::emplace`. When the `work` object is destroyed, its destructor decrements the list's count and, if the count reaches zero after `closed_` is true, atomically swaps out and invokes the finisher. This guarantees the finisher fires exactly once and only after all live async objects are gone.

The `close(Finisher&&)` overload takes a callable; `ServerImpl::close()` passes a lambda that releases the `work_` guard and calls `handler_.onStopped()`. The destructor path (when the server is dropped without an explicit `close()`) calls `ios_.close()` followed by `ios_.join()`, which blocks on a `condition_variable` until `n_ == 0`. The comment in the destructor notes that `onStopped` is deliberately not called in that path.

## Summary of Key Design Choices

The combination of a type-erasing factory, a virtual base interface, and a template implementation cleanly separates the public API surface from the template expansion required by the handler type. The `io_list` pattern avoids both reference cycles and use-after-free in shutdown by ensuring that outstanding async work keeps the objects alive through `shared_ptr` while the list itself holds only weak references. The fd-throttle in `Door` is a practical defensive measure against connection storms that would otherwise surface as cryptic `accept` errors.