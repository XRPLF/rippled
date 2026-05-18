# `aged_set.h` — Time-Indexed Ordered Set Alias

## Role and Purpose

`aged_set.h` is a single-line type-alias header that surfaces the `beast::aged_set` container into user code. Its entire body is:

```cpp
template <class Key, class Clock, class Compare, class Allocator>
using aged_set = detail::aged_ordered_container<false, false, Key, void, Clock, Compare, Allocator>;
```

The two leading `bool` arguments to the underlying template encode the container's personality: `IsMulti=false` (unique keys, like `std::set`) and `IsMap=false` (no mapped value, keys carry the full payload). The entire implementation lives in `detail/aged_ordered_container.h`; this file's only job is to present a clean, std-library-style name with sensible defaults.

## The Underlying `aged_ordered_container`

The real complexity is in `aged_ordered_container`, which maintains **two parallel intrusive data structures** over the same heap-allocated `element` objects:

- A **Boost.Intrusive set** (`cont_type`) — providing O(log n) key lookup, sorted by `Compare`. For `IsMulti=false` this is `boost::intrusive::set`.
- A **Boost.Intrusive list** (`chronological.list`) — an insertion-ordered sequence where the head holds the oldest element and the tail the newest.

Each `element` node inherits from both `boost::intrusive::set_base_hook` and `boost::intrusive::list_base_hook`, so a single allocation simultaneously participates in both structures at zero extra overhead per node.

The `element` struct stores two fields: the `value_type` (just `Key` for a set) and a `time_point when` recording when the element was last inserted or touched. The timestamp is stamped from `clock().now()` at allocation time inside `new_element()`, eliminating any separate timestamp-injection ceremony at the call site.

## Clock Abstraction and Testability

The `Clock` template parameter is not used raw — it is wrapped via `abstract_clock<Clock>`, a reference-semantic wrapper that the container stores as a `std::reference_wrapper<clock_type>`. This indirection is the key to testability: production code passes a real `std::chrono::steady_clock`-based clock, while unit tests can substitute a manually advanced mock clock. All constructors require a clock reference; `aged_ordered_container() = delete` enforces this — there is no default-constructed, clock-free state.

## The `chronological` Memberspace

The `chronological` member object (an instance of the private `chronological_t` class) exposes a separate iterator range over the intrusive list in insertion/touch order. This "memberspace" pattern (from an ACCU article cited in the source) lets user code write:

```cpp
for (auto it = s.chronological.begin(); it != s.chronological.end(); ++it) { ... }
```

while the key-ordered range is accessed through the container's primary `begin()`/`end()`. The chronological iterators support reverse traversal as well, so both oldest-first and newest-first sweeps are O(1) to begin.

## `touch()` — LRU Semantics

The `touch()` member function (available by key or by iterator) updates an element's `when` timestamp and moves it to the **tail** of the chronological list. The iterator-based overload uses `enable_if` to reject reverse iterators at compile time — mutating through a reverse iterator would silently corrupt the list order, so the trait `is_boost_reverse_iterator` is used to catch that at substitution time rather than at runtime.

This design makes `aged_set` a direct building block for LRU caches and time-expiry tables: iterate `chronological.begin()` to find elements that have been untouched longest, erase them until only recently-touched entries remain.

## Iterator Constness for Set Semantics

Because keys in a set must be immutable (mutation would silently corrupt the ordering invariant), `iterator` and `reverse_iterator` are defined as `aged_container_iterator<!IsMap, ...>`. With `IsMap=false`, `!IsMap=true`, so the boolean template argument that controls constness is `true`, making the non-const iterator aliases identical to the const ones. This mirrors `std::set`'s guarantee that even non-const iterators yield `const Key&`.

## Concrete Usage in XRPL

The PeerFinder subsystem uses `aged_set` directly as its `Squelches` type:

```cpp
using Squelches = beast::aged_set<beast::IP::Address>;
```

`ConnectHandouts` inserts an `IP::Address` into the squelch set the moment an outgoing connection attempt is made. The container's dual-index structure lets the caller both do O(log n) duplicate checks (`insert` returns a bool) and sweep expired squelch entries cheaply by walking `chronological.begin()` forward until ages fall within the acceptable window — no separate timestamp map is needed.

## Sibling Variants

The aged container family follows a consistent aliasing pattern. `aged_multiset` sets `IsMulti=true`. `aged_map` and `aged_multimap` set `IsMap=true` (and supply a non-`void` mapped type). The unordered variants (`aged_unordered_set`, etc.) alias `detail::aged_unordered_container` instead, which replaces the intrusive set with a Boost hash table while keeping the same chronological list structure and `touch()` interface.