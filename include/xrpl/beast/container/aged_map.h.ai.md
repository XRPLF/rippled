# `aged_map.h` — Time-Aware Ordered Associative Container

`aged_map.h` is a thin type alias header that exposes `beast::aged_map` as a named type in the `beast` namespace. The file itself contains only the alias declaration; all implementation lives in `detail/aged_ordered_container.h`.

```cpp
template <class Key, class T,
          class Clock    = std::chrono::steady_clock,
          class Compare  = std::less<Key>,
          class Allocator = std::allocator<std::pair<Key const, T>>>
using aged_map = detail::aged_ordered_container<false, true, Key, T, Clock, Compare, Allocator>;
```

The two leading boolean template arguments to `aged_ordered_container` are the compile-time policy switches: `IsMulti = false` (unique keys, mirroring `std::map`) and `IsMap = true` (key-value pairs rather than bare keys). Companion aliases in the same directory follow the same pattern — `aged_multimap` passes `IsMulti = true`, while `aged_set` and `aged_multiset` pass `IsMap = false`.

## What the underlying container provides

`aged_ordered_container` maintains **two simultaneous index structures** over the same set of heap-allocated `element` nodes, using Boost.Intrusive to avoid any extra allocation:

- A **Boost.Intrusive set** (or multiset) keyed on `Key`, providing the standard `O(log n)` ordered-map interface — `find`, `insert`, `erase`, `lower_bound`, `upper_bound`, etc.
- A **Boost.Intrusive doubly-linked list** ordered by insertion or last-touch time, exposed as the `chronological` memberspace.

Each node stores both the `value_type` (`std::pair<Key const, T>`) and a `time_point when` field. When an element is inserted, `when` is set to `clock().now()`. The `touch()` method updates `when` and moves the node to the tail of the chronological list, enabling LRU-style access tracking without any separate bookkeeping.

## Clock abstraction and expiration

The clock template parameter is wrapped through `abstract_clock<Clock>`, which separates the clock's type from the `now()` call. This indirection lets test code inject a manual clock, advancing time deterministically to drive expiration logic — a critical capability in a ledger implementation where time-dependent behavior must be reproducible.

The free function `expire()` in `aged_container_utility.h` demonstrates the intended usage pattern: it walks `chronological.cbegin()` forward, erasing every element whose `when` timestamp predates `clock().now() - age`. Because all insertions append to the back of the chronological list, the front always holds the oldest entry, so this is a linear-time sweep through a naturally ordered sequence — no sorting required.

## Relationship to the container family

`aged_map` is the direct drop-in for `std::map` in contexts where entries must carry an expiration age. The four ordered variants (`aged_map`, `aged_multimap`, `aged_set`, `aged_multiset`) and four unordered variants (`aged_unordered_map`, etc.) form a complete family, all delegating to `aged_ordered_container` or its unordered sibling. Choosing `aged_map` specifically means: unique keys, key-comparator ordering for lookup, and chronological ordering available as a secondary axis for cache management.