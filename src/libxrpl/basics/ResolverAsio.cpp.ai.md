# `ResolverAsio.cpp` — Asynchronous DNS Resolution for XRPL

This file provides XRPL's concrete implementation of asynchronous hostname resolution, `ResolverAsioImpl`, built on top of Boost.Asio's `tcp::resolver`. It sits at the foundation of the peer-discovery and overlay networking subsystems, translating string-form peer addresses (e.g. `r.ripple.com:51235`) into `beast::IP::Endpoint` objects that the rest of the stack can connect to.

## Design Layers

The file introduces two independent pieces before assembling them:

**`AsyncObject<Derived>`** is a CRTP mix-in that answers one recurring problem in Asio code: knowing when every outstanding async handler has finished so the owning object can safely be destroyed. It maintains an `std::atomic<int>` counter, `m_pending`, and exposes two mechanisms for callers to interact with it. The `CompletionCounter` inner class is the primary one — it increments the counter on construction (including copy construction, which is what happens when Asio copies a bound handler into the queue) and decrements it in the destructor; when the decrement reaches zero it calls `asyncHandlersComplete()` on the derived object. The `addReference`/`removeReference` pair provides the same semantics for cases where a plain RAII object isn't the right fit, as used during `start()`. The destructor asserts `m_pending == 0`, making it impossible to accidentally destroy the object with live handlers.

**`ResolverAsioImpl`** inherits from both `ResolverAsio` and `AsyncObject<ResolverAsioImpl>`, combining the abstract resolver interface with the lifecycle tracking. All mutable state — the work queue, resolver, and stop flags — is accessed exclusively on a `boost::asio::strand`, so there is no locking for I/O path operations. A `std::mutex` / `std::condition_variable` pair is used only for the one synchronous blocking call, `stop()`.

## Work Queue and Serial Dispatch

Callers submit a batch of hostnames with a single callback via `resolve()`, which serializes onto the strand and calls `do_resolve()`. There the batch is wrapped in a `Work` item. Critically, names are stored **reversed** in the internal `std::vector` using `std::reverse_copy`; this makes serving the next name from the front of the logical batch a `pop_back()` — O(1) without any shifting. Work items accumulate in a `std::deque<Work>` and are drained sequentially by `do_work()`.

`do_work()` pulls one name at a time, parses it with `parseName()`, and issues a single `m_resolver.async_resolve()` call. After each resolution completes (via `do_finish()`), a new `do_work()` is posted back to the strand. This serial-one-at-a-time pattern is deliberate: it prevents a flood of simultaneous DNS queries from consuming excessive file descriptors or hitting resolver limits, and keeps back-pressure natural — new work items simply wait in the deque.

## Name Parsing Strategy

`parseName()` has a two-tier approach to address ambiguity. It first tries `beast::IP::Endpoint::from_string_checked()`, which correctly handles IPv6 addresses like `[::1]:6006` where a raw colon-split would fail. Only if that returns `nullopt` does it fall back to a generic whitespace-trimmed `host:port` scan using iterators. Whitespace is stripped from both ends before the colon search, and the port separator scan accepts both colons and whitespace, making it tolerant of `"r.ripple.com 51235"` as well as `"r.ripple.com:51235"`. An empty host string after parsing causes the name to be skipped with an error log, but processing of the remaining queue continues.

## Lifecycle and Shutdown

The object starts in a *stopped* state (`m_stopped = true`, `m_pending = 0`). Calling `start()` atomically clears the stopped flag and calls `addReference()`, bumping `m_pending` to 1 — this "lifetime reference" keeps `asyncHandlersComplete()` from firing prematurely while the resolver has no active handlers but is still logically running.

`stop_async()` is idempotent via `m_stop_called.exchange(true)` and posts `do_stop()` to the strand. `do_stop()` clears the work queue, cancels any in-flight Asio resolution (causing pending handlers to be called back with `operation_aborted`), and calls `removeReference()` to drop the lifetime reference. `do_finish()` silently discards `operation_aborted` results, so cancelled resolutions produce no spurious callbacks to user code.

`stop()` is the synchronous variant: it calls `stop_async()` then waits on a `condition_variable` for `m_asyncHandlersCompleted` to become true. This flag is set in `asyncHandlersComplete()` — the callback that fires when `m_pending` reaches zero — under the mutex, so `stop()` returns only after every `CompletionCounter` has been destroyed, i.e., every Asio handler has returned. The destructor then asserts both `m_work.empty()` and `m_stopped`, providing a hard fail-fast if shutdown is skipped.

## Factory and Interface

`ResolverAsio::New()` is the sole construction path, returning a `std::unique_ptr<ResolverAsio>` backed by a `ResolverAsioImpl`. Callers never see the implementation type. The abstract `Resolver` base is pure-virtual with its destructor defined in this translation unit (`Resolver::~Resolver() = default;`), satisfying the ODR requirement for a virtual destructor declared as `= 0` in the header while keeping the vtable anchored to this file.