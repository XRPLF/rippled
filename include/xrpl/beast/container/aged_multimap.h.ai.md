# `aged_multimap.h` — Time-Aware Multi-Value Associative Container

## Purpose and Role

`aged_multimap.h` defines `beast::aged_multimap`, a thin template alias that composes the full `detail::aged_ordered_container` implementation into a familiar, `std::multimap`-like interface extended with per-element timestamps. It lives alongside `aged_map`, `aged_set`, `aged_multiset`, and their unordered counterparts in the `beast` container family, all of which expose the same chronological tracking layer on top of standard ordered or hash-based associative semantics.

## What the Alias Encodes

The entire file reduces to one line:

```cpp
using aged_multimap = detail::aged_ordered_container<true, true, Key, T, Clock, Compare, Allocator>;
```

The two leading `bool` template arguments are the expressive core of `aged_ordered_container`'s design. `IsMulti = true` selects a `boost::intrusive::multiset` as the underlying sorted container, permitting duplicate keys — exactly the semantic difference between `std::map` and `std::multimap`. `IsMap = true` sets the `value_type` to `std::pair<Key const, T>` rather than a bare key, enabling key-to-value mapping. Compare `aged_map`, which passes `<false, true, ...>`, with `aged_multiset`, which passes `<true, false, ...>`: the combinatorial product of those two flags yields all four ordered containers from a single implementation.

## Template Parameters

| Parameter | Default | Role |
|-----------|---------|------|
| `Key` | — | Sorted key type |
| `T` | — | Mapped value type |
| `Clock` | `std::chrono::steady_clock` | Clock used to timestamp insertions and `touch()` calls |
| `Compare` | `std::less<Key>` | Key ordering predicate |
| `Allocator` | `std::allocator<std::pair<Key const, T>>` | Element allocator |

The `Clock` parameter is not used directly; `aged_ordered_container` wraps it in `abstract_clock<Clock>`, an injectable abstraction that allows deterministic testing by substituting a manual clock without recompiling the container.

## What `aged_ordered_container` Provides

Because `aged_multimap` is a pure alias with no added members, all behavior comes from `aged_ordered_container`. Each element internally carries a `time_point` recording when it was inserted or last `touch()`-ed. The container maintains a parallel `boost::intrusive::list` of elements in insertion/touch order; this list is the backbone of the `chronological` memberspace, which exposes `begin()`/`end()` iterators that walk elements from oldest to newest. The intended use case is time-bounded caches: callers iterate the chronological range to expire all entries older than a given threshold, a pattern common in the XRPL fee queue, transaction cache, and node store layers.

The `touch()` operation moves an element to the back of the chronological list without disturbing its position in the sorted key index — an O(1) intrusive list splice. This makes LRU-style eviction efficient without a secondary data structure.

## Design Decision: Alias vs. Subclass

Using a template alias rather than a derived class avoids vtable overhead, prevents accidental slicing, and keeps the public API identical across all four ordered aged-container variants without repeating any forwarding boilerplate. The cost is that `aged_multimap` cannot add member functions; any extension must be done in `aged_ordered_container` itself, guarded where necessary by the `IsMulti` / `IsMap` traits already present there (for example, `pair_value_compare` is compiled into every instantiation but only meaningful when `IsMap = true`).

## Relationship to Sibling Files

`aged_map.h` is structurally identical except for `IsMulti = false`. The two files exist separately — rather than as a single header with a flag parameter — to mirror the `<map>` / `<set>` naming convention from the standard library, making the intent of client code immediately readable at the point of declaration.