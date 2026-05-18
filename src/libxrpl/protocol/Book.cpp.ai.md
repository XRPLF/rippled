# `src/libxrpl/protocol/Book.cpp`

## Role in the System

This file provides the four free-function utilities that give the `Book` type its protocol-layer behavior: validation, string conversion, stream output, and reversal. The file is intentionally thin — `Book` itself is a plain value type (two `Asset` legs plus an optional domain), so all logic here is stateless and algorithmic, delegating to the parallel `Issue`/`Asset` functions wherever possible.

A `Book` models an order book on the XRP Ledger: a directional market between two assets where offers convert the `in` currency to the `out` currency. That directionality is central to why `reversed()` exists as a named operation rather than just a convenience constructor call elsewhere.

## The `Book` Data Model

`Book` (declared in `Book.h`) stores three fields:

- `Asset in` — the asset offered by takers (what they pay).
- `Asset out` — the asset received by takers (what they get).
- `std::optional<uint256> domain` — an optional permissioned-domain identifier, supporting domain-scoped order books introduced as a newer protocol feature.

`Asset` is a variant that can hold either an `Issue` (a classic currency/issuer pair) or an `MPTIssue` (a multi-purpose token). The `Book.h` header builds on top of this by providing equality (`operator==`), three-way comparison (`operator<=>`), `hash_append`, and full `std::hash`/`boost::hash` specializations, making `Book` usable as a key in both ordered and unordered containers throughout the engine.

## `isConsistent`

```cpp
bool isConsistent(Book const& book) {
    return isConsistent(book.in) && isConsistent(book.out) && book.in != book.out;
}
```

This is the primary validation guard for `Book` objects entering the system. It composes `Issue::isConsistent` — which checks that `isXRP(currency) == isXRP(account)`, i.e., a currency is either both-XRP or neither-XRP — across both legs, then adds the additional invariant that the two legs must differ. A book where `in == out` would represent trading a currency against itself: nonsensical and a likely sign of malformed input.

Critically, validation returns `false` rather than throwing. This matches the pattern used throughout the XRPL protocol layer for soft validation: callers decide whether inconsistency is a fatal error or just a rejection signal. In practice, `isConsistent(book)` is called as a hard input guard in the Subscribe RPC handler (`Subscribe.cpp:255`) before processing any subscription, and as an assertion in `getBookBase()` in `Indexes.cpp` where an inconsistent book would produce a corrupted ledger index key.

Notice that `book.domain` is not validated here — its semantic validity (whether the domain actually exists, whether it's accessible) belongs to higher-level transaction processing, not the protocol primitive layer.

## `to_string` and `operator<<`

```cpp
std::string to_string(Book const& book) {
    return to_string(book.in) + "->" + to_string(book.out);
}
```

The format `<in>-><out>` is purely for diagnostic and logging purposes. The arrow makes directionality explicit, which matters because order books are one-way markets. The `operator<<` overload simply delegates to `to_string`, keeping a single formatting path.

This mirrors the `Issue` utility surface exactly — `Issue` has its own `to_string` and `operator<<` — creating a consistent pattern across protocol primitives.

## `reversed`

```cpp
Book reversed(Book const& book) {
    return Book(book.out, book.in, book.domain);
}
```

This function swaps `in` and `out` while preserving `domain` unchanged. It is used in at least two important call sites:

1. **Subscribe/Unsubscribe RPC handlers** — when a client subscribes to a market with the `both` flag, the server subscribes to both `book` and `reversed(book)` so the client receives updates from both the bid and ask sides of the same market.
2. **`BookDirs` tests** — the `reversed` book is used to navigate offer directories from the opposite direction.

The `domain` field carrying through unchanged is a deliberate design: a domain-scoped market is still the same market when viewed from either direction — the scope doesn't flip.

## Design Consistency

The entire file is intentionally minimal. `Book` is a value type, so its utility functions require no state, no resource management, and no complex error handling. The consistency with the `Issue` layer's design — the `isConsistent`/`to_string`/`operator<<` trio — means `Book` behaves uniformly with other protocol primitives, reducing cognitive overhead when working across the protocol layer. Validation returns `bool` (never throws), string conversion is diagnostic-only, and the `reversed` operation is pure (returns a new value, never mutating).