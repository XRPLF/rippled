# `detail/Work.h` — Abstract Interface for Asynchronous HTTP Fetch Tasks

## Role in the System

`Work.h` is the root of a small but architecturally significant abstraction layer inside `xrpld/app/misc/detail/`. It defines the `Work` interface and the shared `response_type` alias that bind together a three-way implementation family — `WorkPlain`, `WorkSSL`, and `WorkFile` — all of which drive validator-list fetching in `ValidatorSite`.

The file exists to give `ValidatorSite` a single, transport-agnostic handle it can store, start, and cancel without knowing whether the underlying fetch goes over plain TCP, TLS, or the local filesystem. This is the classic *command* or *task object* pattern applied to async I/O.

## The `Work` Interface

The class is intentionally minimal: a virtual destructor and two pure virtual methods, `run()` and `cancel()`. There is no state, no constructor arguments, no coupling to Boost.Asio. That sparseness is deliberate — it allows `ValidatorSite` to hold a `std::shared_ptr<detail::Work>` and dispatch `run()` or `cancel()` without touching any transport-specific type.

`cancel()` is as important as `run()`. Validator-list refreshes are timer-driven; when the node is shutting down or a refresh is superseded, the in-flight operation must be abortable cleanly. Because `cancel()` is part of the interface contract, callers never need to downcast or conditionally call transport-specific shutdown methods.

## The `response_type` Alias

```cpp
using response_type = boost::beast::http::response<boost::beast::http::string_body>;
```

This type alias lives at namespace scope inside `xrpl::detail` rather than inside the class. That placement is meaningful: the callback signature used by `WorkBase<Impl>` passes a `response_type&&` to callers, and `ValidatorSite.cpp` references `detail::response_type` directly in its `onSiteFetch` handler. Centralizing it in this header avoids duplicating the Boost.Beast template instantiation in every consumer.

`string_body` is chosen because validator list payloads are JSON text documents of moderate size. A streaming or dynamic-body alternative would add complexity without benefit here.

## The Inheritance Hierarchy

`Work.h` is the apex. The concrete types form a CRTP chain:

- `WorkBase<Impl>` inherits `Work` and implements the common async HTTP GET flow: DNS resolution → TCP connect → (optional TLS handshake via `impl()`) → HTTP write → HTTP read → callback. It uses `boost::asio::strand` to serialize all handlers for a given request.
- `WorkPlain` extends `WorkBase<WorkPlain>` for plain TCP. It exposes the raw `socket_` as `stream()`.
- `WorkSSL` extends `WorkBase<WorkSSL>` for TLS. It wraps `socket_` in a `boost::asio::ssl::stream` and adds an `onHandshake` step between connect and HTTP write.
- `WorkFile` (not shown in detail) handles `file://` URLs directly without network I/O, satisfying the same `Work` interface for local validator lists used in testing or air-gapped deployments.

The CRTP in `WorkBase` — `impl().stream()` and `impl().onConnect()` — lets the base class drive the I/O state machine while delegating transport-specific operations to the concrete subclass without virtual dispatch on the hot path. The `Work` virtual interface is only exercised by `ValidatorSite`, not inside the async handler chain itself.

## Lifetime and Cancellation Contract

`WorkBase`'s destructor fires the callback with `boost::system::errc::not_a_socket` if it is destroyed while the callback is still live (i.e., before a response or explicit failure was delivered). This defensive pattern ensures `ValidatorSite` always receives exactly one callback invocation per `Work` object — either a successful response, a failure from `fail()`, or this destructor sentinel — preventing silent dropped fetches.

`cancel()` posts to the strand if not already running on it, then cancels both the resolver and the socket. This two-phase approach is necessary because Boost.Asio resolver and socket cancellation are separate operations, and the strand ensures the cancel runs after any already-queued handlers complete.

## Why This Design Over Alternatives

A simpler approach would have `ValidatorSite` branch on URL scheme everywhere. The `Work` abstraction moves that branch to a single factory site in `ValidatorSite::makeRequest` (choosing `WorkSSL`, `WorkPlain`, or `WorkFile`) and then operates uniformly. This keeps the site-management logic — retry counts, redirect following, refresh scheduling — free of transport details, which is the primary maintenance benefit.