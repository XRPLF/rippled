# `include/xrpl/ledger/BookListeners.h`

## Role in the System

`BookListeners` is the fan-out layer that connects the XRPL decentralized exchange (DEX) to WebSocket subscribers. When a transaction modifies offers in an order book, the server must notify every client that has subscribed to that book via the `subscribe` RPC command. `BookListeners` is the per-book object that owns and manages that subscriber set, and dispatches serialized transaction JSON to each of them.

One `BookListeners` instance exists for each `Book` (currency pair) that has at least one active subscriber. The `OrderBookDB` interface — and its concrete `OrderBookDBImpl` — is the registry that creates and looks up these instances. When `OrderBookDBImpl::processTxn()` encounters a modified offer node in a transaction's metadata, it resolves the affected book to a `BookListeners` pointer and calls `publish()`.

## Class Design

The class is deliberately minimal: a protected `hash_map` keyed by subscriber sequence number and a `recursive_mutex`. Subscribers are stored as `InfoSub::wptr` (i.e., `weak_ptr<InfoSub>`), not as strong pointers. This is the critical ownership decision — `BookListeners` must not keep subscribers alive. `InfoSub` objects are owned by the connection layer; if a client disconnects, its `InfoSub` is destroyed. The weak pointer then expires, and `BookListeners` detects this lazily.

Subscriber identity is tracked by `uint64_t` sequence number (`InfoSub::getSeq()`) rather than by raw pointer. This allows `removeSubscriber()` to erase by ID without needing the original `shared_ptr`, which is important during `InfoSub` destruction where the subscription cleanup path may not have a live pointer available.

## The `publish()` Method and Duplicate Suppression

```cpp
void publish(MultiApiJson const& jvObj, hash_set<std::uint64_t>& havePublished);
```

The `havePublished` set is the key non-obvious mechanism here. A single transaction can touch multiple order books simultaneously — for example, a cross-currency payment may affect both the XRP/USD book and the USD/BTC book. If a client has subscribed to both books, a naïve implementation would deliver the same transaction notification twice.

`OrderBookDBImpl::processTxn()` creates one `havePublished` set per transaction and passes it (by reference) to every `BookListeners::publish()` call for that transaction. Inside `publish()`, the code calls `havePublished.emplace(p->getSeq()).second`, which returns `true` only when the subscriber ID was freshly inserted. Only then is the message actually sent via `p->send()`. Subsequent calls for the same transaction on other books that share a subscriber are silently skipped.

## Multi-Version JSON Delivery

`publish()` receives a `MultiApiJson const& jvObj` rather than a plain `Json::Value`. `MultiApiJson` (aliased from `detail::MultiApiJson<apiMinimumSupportedVersion, apiMaximumValidVersion>`) is a fixed-size array of `Json::Value` objects, one per supported API version. The transaction serialization layer fills this once, before `publish()` is ever called.

Inside `publish()`, each subscriber's API version is fetched via `p->getApiVersion()`, and the correct per-version JSON is selected using `jvObj.visit(p->getApiVersion(), [&](Json::Value const& jv) { p->send(jv, true); })`. This avoids re-serializing the transaction for every subscriber and every API version — the version-specific JSON objects are computed once upstream and then indexed cheaply here.

## Lazy Expiry of Dead Subscribers

During `publish()`, after `it->second.lock()` fails (the `InfoSub` has been destroyed), the dead entry is erased in-place via `it = mListeners.erase(it)`. This means the map self-cleans during normal operation without requiring a separate sweep or background GC pass. `addSubscriber()` and `removeSubscriber()` handle explicit lifecycle events (connect and disconnect), but the lazy erase in `publish()` provides a safety net for connections that disappear without a clean unsubscribe.

## Locking

`std::recursive_mutex` is chosen over a plain `std::mutex`. In practice, none of the three public methods call each other (so no direct re-entrancy exists on `mLock`), but `recursive_mutex` leaves the door open for future callers that might already hold the lock on a different code path. All three public methods take a `std::lock_guard` for the entirety of their execution, which keeps the implementation straightforward at the cost of holding the lock across `p->send()` during publish. This means subscriber callbacks execute under the lock — a tradeoff that favors simplicity and correctness over throughput on high-subscriber-count books.

## Relationship to `OrderBookDB`

`BookListeners` has no knowledge of which `Book` it belongs to — it is a pure subscriber container. `OrderBookDB` (specifically `OrderBookDBImpl`) maps `Book` → `BookListeners::pointer` in its own `mListeners` hash map, and provides `getBookListeners()` and `makeBookListeners()` to retrieve or create them. The `shared_ptr<BookListeners>` type alias (`BookListeners::pointer`) is how callers hold references without exposing raw owning pointers.