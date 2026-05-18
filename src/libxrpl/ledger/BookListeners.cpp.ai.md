# `BookListeners.cpp` — Order Book Subscription Fan-Out

`BookListeners.cpp` implements the `BookListeners` class, which manages the set of WebSocket/RPC clients subscribed to trade events on a specific order book (a currency pair). It lives in `src/libxrpl/ledger/` and its interface is declared in `include/xrpl/ledger/BookListeners.h`.

The XRPL subscription model lets clients call `subscribe` with a list of currency-pair books. When a transaction affects any of those books — a new offer placed, an offer consumed, a cross-currency payment executed — the server must push a JSON notification to every interested subscriber. `BookListeners` is the per-book half of that fan-out: one instance exists per tracked `Book`, and it holds the list of `InfoSub` objects that care about that particular pair.

## Subscriber Storage and Lifetime

Subscribers are stored as `InfoSub::wptr` (i.e., `std::weak_ptr<InfoSub>`) values in a `hash_map<std::uint64_t, InfoSub::wptr> mListeners`, keyed by each subscriber's monotonically-increasing sequence number (`mSeq` from `InfoSub`). Using a `weak_ptr` rather than a `shared_ptr` is the critical design choice here: `BookListeners` must not extend a subscriber's lifetime. When the network layer tears down a client connection, the corresponding `InfoSub` should be destroyed regardless of how many book subscription lists it appears in. The `weak_ptr` allows the map to hold a reference without keeping the object alive.

`addSubscriber()` stores the incoming `InfoSub::ref` (a `shared_ptr const&`) as a weak reference, keyed by `sub->getSeq()`. `removeSubscriber()` erases by sequence number — the caller supplies only the `uint64_t` key, never the object itself, which is safe to do even after the `InfoSub` has been destroyed. Both methods take a `std::lock_guard` on `mLock` (declared as a `std::recursive_mutex` in the header, allowing safe reentrant locking if the mutex is ever held on the calling stack).

## `publish()` — Lazy Cleanup and Deduplication

```cpp
void BookListeners::publish(MultiApiJson const& jvObj, hash_set<std::uint64_t>& havePublished)
```

`publish()` combines three responsibilities into a single locked pass over `mListeners`:

**Lazy expiry cleanup.** For each entry, it calls `it->second.lock()` to attempt to promote the `weak_ptr` to a `shared_ptr`. If `lock()` returns null, the subscriber is gone; the entry is erased in-place with `it = mListeners.erase(it)` and iteration continues. This means `mListeners` never requires a separate housekeeping pass — dead entries are evicted the next time any transaction touches that book. The only downside is that a book with no traffic after disconnect will retain stale entries indefinitely, but such books also generate no publish load, so the tradeoff is sound.

**Cross-book deduplication.** The `havePublished` parameter is a `hash_set<std::uint64_t>` owned by the caller and shared across all `BookListeners::publish()` calls for a single transaction. A subscriber that has registered for multiple books affected by one transaction would otherwise receive the same JSON payload once per matching book. Before sending, `publish()` calls `havePublished.emplace(p->getSeq())`; `emplace` returns a pair whose `.second` is `true` only if the insertion actually happened (i.e., the sequence number was not already present). Only on first occurrence does the code proceed to send. This deduplication is invisible to the subscriber and requires no coordination between individual `BookListeners` instances — the caller provides the shared bookkeeping set.

**API-version-aware serialization.** Rather than storing a single pre-serialized `Json::Value`, `publish()` receives a `MultiApiJson const&`. `MultiApiJson` is a template struct (`detail::MultiApiJson<MinVer, MaxVer>`) holding an array of `Json::Value` objects — one per supported API version. The `jvObj.visit(p->getApiVersion(), lambda)` call selects the version-specific element at index `apiVersion - MinVer` and passes it to the lambda, which calls `p->send(jv, true)`. This means the caller prepares a single `MultiApiJson` (possibly with version-specific field differences) and each subscriber automatically receives the representation matching its negotiated API version, without any branching in `publish()` itself.

## Relationship to `OrderBookDB`

`BookListeners` instances are created and retrieved through `OrderBookDB` (declared in `include/xrpl/ledger/OrderBookDB.h`), which tracks all live order books in the current ledger. When `OrderBookDB::processTxn()` processes an accepted transaction, it identifies which `Book` objects it touched, fetches the corresponding `BookListeners` via `getBookListeners()`, and calls `publish()` on each, threading the same `havePublished` set through every call. `BookListeners` is therefore a leaf node in that pipeline — it does no ledger parsing or book detection, only subscriber dispatch.

## Thread Safety

Every public method locks `mLock` with `std::lock_guard`, making `addSubscriber`, `removeSubscriber`, and `publish` safe to call concurrently. The mutex is `std::recursive_mutex` rather than `std::mutex`, which permits a thread that already holds the lock to reacquire it safely — a defensive choice given that callbacks from `InfoSub::send()` could theoretically re-enter ledger notification paths. The RAII guard ensures the mutex is released even if `send()` throws.