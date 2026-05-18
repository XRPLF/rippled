# `aged_unordered_set.h`

## Role and Purpose

This header provides `beast::aged_unordered_set`, a thin type alias that composes the generic `detail::aged_unordered_container` into a familiar `std::unordered_set`-like interface enriched with timestamp tracking. It exists to give XRPL components a standard-looking container for building time-aware caches — structures where entries expire after a configurable age rather than being evicted by capacity alone.

The file itself is only 19 lines; all logic lives in `detail/aged_unordered_container.h`.

## The Two-Index Architecture

Every element in the container participates simultaneously in two Boost.Intrusive data structures: a `boost::intrusive::unordered_set` (or `unordered_multiset`) for O(1) hash-based key lookup, and a `boost::intrusive::list` that maintains insertion/touch order. Because Boost.Intrusive stores the hook pointers directly inside the `element` struct, there is no per-element allocation overhead beyond the element itself — both indexes share the same heap nodes.

This dual-membership is the central design decision. An ordinary `std::unordered_set` plus a side `std::list` would require coordinated allocations and pointer synchronization; the intrusive approach makes insertion, erasure, and `touch()` all atomic with respect to both indexes at once.

## Template Parameters

```cpp
template <
    class Key,
    class Clock = std::chrono::steady_clock,
    class Hash = std::hash<Key>,
    class KeyEqual = std::equal_to<Key>,
    class Allocator = std::allocator<Key>>
using aged_unordered_set = detail::aged_unordered_container<
    false, false, Key, void, Clock, Hash, KeyEqual, Allocator>;
```

The two leading booleans hard-wire `IsMulti=false` (unique keys, no duplicates) and `IsMap=false` (set semantics — `value_type` is `Key` alone, not a `pair`). The `void` passed for `T` is vestigial; `aged_unordered_map` passes a real mapped type in that slot. The `Clock` parameter is not used to construct a default clock — the container holds an `abstract_clock<Clock>&` reference that callers must supply, keeping clock behaviour injectable for tests.

## Timestamp Semantics and Expiry

On insertion the container stamps the new element with `clock().now()`. The `touch(key)` method — absent from standard containers — looks up the element, records the current time, and moves it to the back of the chronological list in O(1) time. This implements LRU-style refresh semantics without a separate data structure.

The companion free function `expire(container, age)` in `aged_container_utility.h` iterates `chronological.cbegin()` forward, erasing elements whose `when` timestamp is older than `(now - age)`. Because the list is ordered oldest-first, the loop can terminate as soon as it finds a live entry, making bulk expiry proportional to the number of expired items rather than the total container size.

## Relationship to Sibling Types

`aged_unordered_multiset` is the identical alias with `IsMulti=true`, allowing duplicate keys. `aged_unordered_map` and `aged_unordered_multimap` swap `IsMap` to `true` and supply a real `T`. The ordered variants (`aged_set`, `aged_map`) delegate to `detail::aged_ordered_container` instead, which uses `boost::intrusive::set` for key lookup while sharing the same chronological-list pattern.