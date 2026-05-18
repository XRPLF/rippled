# `WorkBase.h` — CRTP Foundation for Asynchronous HTTP GET Requests

## Role in the System

`WorkBase` is the shared implementation backbone for the XRPL node's outbound HTTP client used when fetching validator list files. The `ValidatorSite` subsystem needs to periodically download signed validator lists from remote URLs (https, http, or file paths). Rather than duplicating the full async pipeline for plain TCP and TLS connections, `WorkBase` encodes the common async state machine — DNS resolution, TCP connection, HTTP request writing, HTTP response reading — and leaves only transport-layer differences to concrete subclasses.

The two concrete subclasses are `WorkPlain` (plain TCP, used for `http://` URLs) and `WorkSSL` (TLS, used for `https://` URLs). A third sibling, `WorkFile`, handles `file://` URLs and is entirely independent, inheriting directly from `Work` without using `WorkBase`.

## CRTP Design

`WorkBase<Impl>` uses the Curiously Recurring Template Pattern: the template parameter `Impl` is always the concrete derived class itself (`WorkPlain` or `WorkSSL`). The `impl()` helper downcasts `this` to `Impl&`, enabling the base to invoke two customization points on the concrete type without virtual dispatch:

- `impl().onConnect(ec)` — called by the base when TCP connection completes. `WorkPlain` proceeds directly to `onStart()`; `WorkSSL` initiates a TLS handshake first.
- `impl().stream()` — returns the writable/readable I/O stream. `WorkPlain` returns the raw `socket_type&`; `WorkSSL` returns a `boost::asio::ssl::stream<socket_type&>`.

Both concrete classes also inherit `std::enable_shared_from_this<Impl>`, and `WorkBase` uses `impl().shared_from_this()` exclusively when binding callbacks. This is the key reason CRTP is preferred over a single virtual class: `shared_from_this()` must return `shared_ptr<Impl>`, not `shared_ptr<WorkBase<Impl>>`, so calling it through the concrete type ensures correct reference counting.

The concrete classes declare `friend class WorkBase<Impl>` to allow the base to call their private `onConnect()` and `stream()` members, keeping those implementation details out of the public API.

## Async State Machine

The lifecycle of a single HTTP GET proceeds through this chain, all serialized on a single `boost::asio::strand`:

1. **`run()`** — Initiates async DNS resolution via `resolver_.async_resolve()`. If not already on the strand, it re-posts itself to the strand first. This cross-thread safety pattern repeats in `cancel()`.

2. **`onResolve()`** — On success, calls `boost::asio::async_connect()` against the full set of resolved endpoints.

3. **`onConnect()`** (base) → **`impl().onConnect()`** — Records the successfully connected endpoint in `lastEndpoint_`, then delegates to the concrete type. This two-level dispatch is what allows `WorkSSL` to interpose a TLS handshake before the HTTP exchange begins.

4. **`onStart()`** — Constructs the HTTP/1.1 GET request with the `Host` header and sets the `User-Agent` to `BuildInfo::getFullVersionString()` (the rippled version string). Writes the request to `impl().stream()`.

5. **`onRequest()`** — After the request is flushed, issues an async read into `readBuf_` (a `boost::beast::multi_buffer`) to receive the full HTTP response into `res_` (a `response_type` with string body).

6. **`onResponse()`** — Closes the socket, fires the callback with the response, and nullifies `cb_` to prevent double-invocation.

## Callback Contract and Error Handling

The callback signature `void(error_code const&, endpoint_type const&, response_type&&)` carries three values: the outcome, the resolved endpoint (for connection prioritization on retry), and the response body. Callers can distinguish success from transport failure using the error code, and can use `lastEndpoint_` to prefer the same server on subsequent requests.

`fail()` is the single failure exit point: it fires the callback with the error and then nullifies `cb_`. This nullification is the guard against double-invocation — every subsequent path in the state machine first checks `if (cb_)`. The destructor also checks and fires `cb_` if it was never cleared, reporting `not_a_socket` as the error code. This ensures the caller always receives exactly one callback regardless of how the object is destroyed — whether cleanly after `onResponse()` or abnormally when cancelled or dropped.

`cancel()` is safe to call from any thread. It cancels the resolver and the socket (suppressing the socket's error code since cancellation is intentional), relying on the strand dispatch to serialize with other async operations.

The `close()` helper shuts down only the send direction of the socket before closing it. This is a graceful TCP half-close: it signals to the remote server that the client is done sending, allowing the server to flush any remaining data, before the connection is fully torn down.

## Thread Safety

All async operations are bound to a single `boost::asio::strand<io_context::executor_type>`. The strand guarantees that no two handlers execute concurrently, even when the underlying `io_context` is driven by a thread pool. The `run()` and `cancel()` entry points both check `strand_.running_in_this_thread()` and re-post themselves if needed, making it safe to call from arbitrary threads without external locking.

## Relationship to `ValidatorSite`

`ValidatorSite.cpp` constructs `WorkPlain` or `WorkSSL` using `std::make_shared<>` depending on URL scheme, then calls `->run()`. The `lastEndpoint` and `lastStatus` constructor parameters carry state from the previous fetch cycle, allowing the validator site fetcher to track which server endpoint was last used and whether it was successful — supporting rudimentary connection affinity or retry strategies at the higher layer.