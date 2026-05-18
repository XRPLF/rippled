# `aged_unordered_multiset.h`

This file defines `beast::aged_unordered_multiset` as a one-line template alias within the `beast` namespace. The entire substance lives in a single `using` declaration that wires the five public template parameters into the shared `detail::aged_unordered_container` engine.

## Role in the Container Family

The `beast` container directory provides eight "aged" associative containers, each a thin alias over one of two internal engines:

| Alias | `IsMulti` | `IsMap` | Engine |
|---|---|---|---|
| `aged_unordered_set` | `false` | `false` | `aged_unordered_container` |
| **`aged_unordered_multiset`** | **`true`** | **`false`** | **`aged_unordered_container`** |
| `aged_unordered_map` | `false` | `true` | `aged_unordered_container` |
| `aged_unordered_multimap` | `true` | `true` | `aged_unordered_container` |

`aged_unordered_multiset` differs from `aged_unordered_set` solely by passing `IsMulti = true`, which routes the internal `boost::intrusive::unordered_set` to `boost::intrusive::unordered_multiset`, permitting duplicate keys. The `IsMap = false` flag collapses the `value_type` to `Key` alone (rather than `std::pair<Key const, T>`), and the `T` slot is left as `void`.

## Template Parameters

```cpp
template <
    class Key,
    class Clock    = std::chrono::steady_clock,
    class Hash     = std::hash<Key>,
    class KeyEqual = std::equal_to<Key>,
    class Allocator = std::allocator<Key>>
```

The `Clock` parameter is not used raw; the engine wraps it in `abstract_clock<Clock>`, an XRPL-internal abstraction that allows test code to substitute a controllable mock clock. This is the primary reason the clock is a template parameter rather than a constructor argument — the clock policy must be baked into the type so that the `time_point` typedef is consistent throughout the container.

## What the Underlying Engine Provides

`detail::aged_unordered_container` is built on two `boost::intrusive` structures that share a single pool of heap-allocated `element` nodes:

1. A `boost::intrusive::unordered_multiset` (because `IsMulti = true`) that handles O(1) average lookup, insertion, and erasure by key.
2. A `boost::intrusive::list` that maintains all elements in insertion/touch order.

Each `element` node embeds both the intrusive hooks for the unordered set and the list, plus a `value` (the `Key` itself for a set) and a `time_point when`. The dual-structure design means the chronological list costs no extra allocation — the list links are stored inside the same node used by the hash table.

The container surfaces a `chronological` memberspace exposing begin/end iterators over the list, enabling callers to walk elements from oldest to newest (or in reverse). This makes `aged_unordered_multiset` a natural primitive for time-bounded caches and expiry queues: insertion order is preserved for free, and `touch()` moves an element to the tail of the chronological list, providing LRU semantics without a separate data structure.

## Design Rationale

The alias-over-engine pattern keeps the public API surface minimal. Because the only difference between the four unordered containers is a pair of `bool` template flags, all correctness fixes, performance tuning, and feature additions are automatically shared. The alternative — four separate class templates — would require maintaining four near-identical implementations. The cost is that the engine's template signature is slightly opaque (`IsMulti`, `IsMap`, `Key`, `T`, `Clock`, ...), which is hidden from users behind the clean four-parameter alias.

The `T = void` slot for set variants is structurally unused; the engine uses `std::conditional<IsMap, std::pair<Key const, T>, Key>` to compute `value_type`, so `void` never materialises in a real type.