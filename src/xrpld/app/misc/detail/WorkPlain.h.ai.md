# `WorkPlain.h` — Plain TCP Work Implementation

`WorkPlain` is the plain-TCP (unencrypted) concrete implementation of the asynchronous HTTP work hierarchy used by the XRPL validator site fetching infrastructure. It lives in `xrpl::detail` alongside its counterpart `WorkSSL` (HTTPS) and `WorkFile` (filesystem), and is selected by `ValidatorSite.cpp` whenever a validator list endpoint uses an `http://` URL scheme.

## Role in the Work Hierarchy

The validator site subsystem abstracts transport behind a three-class family:

- `Work` — root interface with `run()` and `cancel()`.
- `WorkBase<Impl>` — CRTP template implementing the full async state machine: DNS resolution, TCP connection, HTTP request write, HTTP response read, and callback delivery.
- `WorkPlain` / `WorkSSL` / `WorkFile` — concrete leaves providing transport-specific behaviour.

`WorkPlain` is the simplest leaf. Because plain TCP needs nothing beyond an established socket before HTTP can begin, its role within the CRTP contract reduces to exactly two methods.

## CRTP Customization Points

`WorkBase<Impl>` calls two methods on the derived type — without virtual dispatch — using a `static_cast` through `impl()`:

- **`stream()`** — `WorkBase` passes this to Boost.Beast's `async_write` and `async_read` calls. `WorkPlain::stream()` returns a plain `socket_type&` (a `boost::asio::ip::tcp::socket`). In contrast, `WorkSSL::stream()` returns an `ssl::stream<socket_type&>`, which is why the customization point exists at all.
- **`onConnect(error_code)`** — called by `WorkBase::onConnect` (the base-class overload that captures the resolved endpoint) after a successful TCP connection. For `WorkPlain`, this is a two-line method: if `ec` is set, propagate failure via `fail(ec)`; otherwise, call `onStart()` to begin the HTTP exchange immediately. `WorkSSL` inserts a TLS handshake step here before calling `onStart()`.

`WorkBase<WorkPlain>` is declared a `friend` so it can invoke the private `onConnect()` and `stream()` methods. This keeps the lifecycle hooks invisible outside the base, enforcing that external callers only ever interact through the `Work` interface.

## Lifetime Management

`WorkPlain` inherits from `std::enable_shared_from_this<WorkPlain>` because every async Boost.Asio operation captures the owning `shared_ptr` in its completion handler. `WorkBase::run()` and every subsequent handler call `impl().shared_from_this()` to extend the object's lifetime until the entire chain completes: resolve → connect → write → read → callback. Without this pattern, the object could be destroyed while outstanding handlers still hold function pointers into it.

## Design Decisions

All method bodies are defined inline in the header. Given that the constructor is a single delegating initializer and `onConnect` is two lines, a separate `.cpp` file would be nearly empty. `WorkSSL`, by contrast, has its own translation unit because `onConnect` must initiate an asynchronous TLS handshake — non-trivial enough to warrant separation.

The constructor parameters `lastEndpoint` and `lastStatus` carry state from the previous fetch cycle. `ValidatorSite` uses these to track which resolved endpoint was last used successfully, enabling connection affinity — a hint to prefer the same server on the next refresh — without encoding that logic inside `WorkPlain` itself.

## Relationship to Sibling Files

Understanding `WorkPlain` in isolation is only half the picture. The real logic lives in `WorkBase.h`, which owns the entire async state machine and calls back into `WorkPlain` only at the two customization points described above. `Work.h` provides the `response_type` alias (`boost::beast::http::response<string_body>`) and the pure-virtual interface that lets `ValidatorSite` hold any of the three work types as a uniform `shared_ptr<Work>`.