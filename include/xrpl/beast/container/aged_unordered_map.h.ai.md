# `aged_unordered_map.h`

## Role in the System

`aged_unordered_map.h` is a thin alias header that exposes `beast::aged_unordered_map` — a hash-based associative container that enriches every stored element with a `time_point` timestamp. It is the unordered-map variant in a family of "aged container" types (`aged_map`, `aged_set`, `aged_unordered_set`, etc.) and serves as the primary building block for time-aware caches throughout the XRPL codebase.

The file itself is minimal: it configures the underlying `detail::aged_unordered_container` policy template with `IsMulti=false, IsMap=true` and re-exports it as a friendly public alias with the familiar `std::unordered_map` template parameter order.

## What It Actually Defines

```cpp
template <
    class Key,
    class T,
    class Clock = std::chrono::steady_clock,
    class Hash = std::hash<Key>,
    class KeyEqual = std::equal_to<Key>,
    class Allocator = std::allocator<std::pair<Key const, T>>>
using aged_unordered_map =
    detail::aged_unordered_container<false, true, Key, T, Clock, Hash, KeyEqual, Allocator>;
```

The two boolean policy flags select the four possible specializations of `aged_unordered_container`:

| `IsMulti` | `IsMap` | Result |
|-----------|---------|--------|
| `false`   | `true`  | `aged_unordered_map` — unique keys, key+value pairs |
| `true`    | `true`  | `aged_unordered_multimap` — duplicate keys, key+value pairs |
| `false`   | `false` | `aged_unordered_set` — unique keys only |
| `true`    | `false` | `aged_unordered_multiset` — duplicate keys only |

`aged_unordered_multimap` differs only in passing `IsMulti=true`, routing to `boost::intrusive::make_unordered_multiset` internally.

## The Underlying Data Structure

`aged_unordered_container` maintains **two simultaneous Boost.Intrusive data structures** over the same set of heap-allocated `element` nodes:

1. A **`boost::intrusive::unordered_set`** (or `unordered_multiset`) for O(1) key lookup. Buckets are owned by an internal `Buckets` helper that performs safe rehashing — it swaps to a temporary vector before resizing downward to avoid destroying non-empty buckets.

2. A **`boost::intrusive::list`** for temporal ordering. Every insertion appends the new element to the back of this list; the front always holds the oldest entry.

Each `element` inherits from both an `unordered_set_base_hook` and a `list_base_hook`, so it participates in both intrusive structures with zero additional memory. Timestamps (`time_point when`) are stored directly in the element alongside its `value_type`.

This dual-structure design is why the container can efficiently serve both hash-based lookup and age-based eviction — no secondary index or sorted tree is needed.

## Temporal Operations

The `chronological` nested object (implementing the "memberspace" idiom from ACCU Journal #1527) provides a separate iterator range over all elements in insertion order, oldest-first. Iterators expose a `.when()` accessor returning the element's `time_point`.

`touch(iterator)` or `touch(key)` updates an element's timestamp to `clock().now()` and moves it to the back of the chronological list — making it the newest element again. This is the key primitive for LRU-style cache semantics.

The free function `expire(container, age)` in `aged_container_utility.h` walks the chronological list from the front and erases all entries whose timestamp is older than `clock().now() - age`. Because the list is maintained in insertion/touch order, expired items are always at the front; `expire` stops as soon as it hits an element that is young enough, making eviction O(expired) rather than O(n).

## Clock Abstraction

The `Clock` template parameter is wrapped in `abstract_clock<Clock>`, a virtual interface. The container holds a `std::reference_wrapper<clock_type>` rather than owning the clock. This means:

- Callers inject the clock at construction time.
- In production, `std::chrono::steady_clock` is the default.
- In tests, a mock clock can be passed in to control time explicitly without sleep.

## Configuration Compression

All stateless policy objects — `ValueHash` (wrapping `Hash`), `KeyValueEqual` (wrapping `KeyEqual`), and `ElementAllocator` — are composed into a single private `config_t` object using empty-base optimization (`beast::detail::empty_base_optimization`). For the common case where `Hash`, `KeyEqual`, and `Allocator` are all default-constructed stateless types, `config_t` contributes zero bytes of overhead beyond the clock reference.

## Relationship to Other Files

- **`detail/aged_unordered_container.h`** — the full implementation; this header is only safe to include through the public aliases.
- **`aged_unordered_multimap.h`** — the `IsMulti=true` sibling, identical layout.
- **`aged_container_utility.h`** — provides the `expire()` free function, the standard consumer of `chronological` iterators.
- **`aged_container.h`** — declares the `is_aged_container` trait used by `expire()` to constrain its template parameter.