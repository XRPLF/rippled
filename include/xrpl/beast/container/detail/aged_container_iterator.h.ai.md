# `aged_container_iterator.h` — Iterator Adapter for Time-Aware Containers

## Role and Purpose

This file defines the `aged_container_iterator<is_const, Iterator>` template in `beast::detail`, the single iterator type shared by all of beast's aged container family (`aged_ordered_container` and `aged_unordered_container`). Its job is to sit in front of a Boost.Intrusive iterator and translate what the underlying intrusive data structure exposes into what a user of an `aged_set`, `aged_map`, or their unordered equivalents expects to see.

Each element in an aged container is stored as an internal `element` struct containing both a `value` (the user-visible key or key–value pair) and a `when` timestamp. `aged_container_iterator` strips away this implementation detail: dereferencing the iterator yields only `value`, while the timestamp remains accessible through the dedicated `when()` method. This separation keeps the iterator compliant with the standard container model while still supporting the core feature that distinguishes aged containers from their standard-library counterparts.

## The `stashed` Indirection Trick

A subtle design challenge arises because the iterator must resolve `value_type` and `time_point` without being able to see the full container class declaration — the container is a complex seven-parameter template, and the iterator lives in a separate `detail` header. The solution is the nested `stashed` struct inside `element`:

```cpp
struct stashed {
    using value_type = typename aged_ordered_container::value_type;
    using time_point = typename aged_ordered_container::time_point;
};
```

`aged_container_iterator` harvests both types through `Iterator::value_type::stashed::*` without needing to `#include` the container definition. This is not a runtime mechanism — `stashed` has no data members, only type aliases — but it cleanly threads the required type information through the Boost.Intrusive iterator's own `value_type` trait, solving the circular-include problem entirely in the type system.

## Const-Correctness Enforcement

The `is_const` boolean template parameter drives a `std::conditional` that selects `value_type const` or `value_type` as the reference and pointer targets. Callers cannot simply assign a `const_iterator` to an `iterator`; the constructors and assignment operator all use `std::enable_if` to reject `other_is_const == true && is_const == false` combinations at compile time.

There are two distinct conversion constructors because two situations arise:

1. **Same `Iterator`, different `is_const`** — handled implicitly (the non-explicit constructor at line 48). This is the usual non-const-to-const promotion that mirrors `std::vector::iterator` → `std::vector::const_iterator`.
2. **Different `Iterator` type** (e.g., forward iterator vs. reverse iterator) — handled by an `explicit` constructor (line 38) so that reverse-to-forward conversion cannot happen accidentally. `std::is_same<Iterator, OtherIterator>::value == false` is required in the `enable_if`, preventing the explicit constructor from shadowing the implicit one when the iterator types are identical.

This two-constructor scheme means you can write `const_iterator ci = iter;` naturally, but converting a `reverse_iterator` to a plain `iterator` requires an explicit cast — matching the semantics of `std::reverse_iterator`.

## The `!IsMap` Convention

The containers instantiate the iterator type with a slightly counter-intuitive convention:

```cpp
// In aged_ordered_container:
using iterator = aged_container_iterator<!IsMap, cont_type::iterator>;
```

For a **set** (`IsMap = false`), `is_const = true`: the element type is the key itself, so it must be immutable. For a **map** (`IsMap = true`), `is_const = false`: `value_type` is `pair<Key const, T>`, the key is already const inside the pair, and the mapped value `T` remains mutable through `iterator`. `const_iterator` is always `is_const = true` regardless.

## Temporal Access via `when()`

The `when()` method returns a `const` reference to `m_iter->when` — the `time_point` recorded when the element was inserted or last `touch()`-ed. This is the axis that makes aged containers useful for LRU caches, expiry queues, and rate-limited tables: code can traverse `chronological::begin()` → `chronological::end()` and evict elements whose `when()` is older than a threshold, without any external bookkeeping structure.

## Friend Boundaries and the Private Constructor

The primary constructor from a raw `OtherIterator` (line 136) and the `iterator()` accessor (line 142) are both `private`, friend-gated to `aged_ordered_container` and `aged_unordered_container`. This means only the container itself can wrap an intrusive iterator into a public-facing `aged_container_iterator`, and only the container can later unwrap one (needed for `erase()` and `touch()` implementations that must pass the raw intrusive iterator back into Boost.Intrusive's API). User code is never exposed to the underlying `element` layout.

The `friend class aged_container_iterator<bool, class>` declaration lets any instantiation of the iterator access `m_iter` of any other instantiation, which is necessary for the cross-`is_const` conversion constructors and comparison operators to work without a public getter.

## Iterator Category and Bidirectionality

`iterator_category` is inherited directly from `std::iterator_traits<Iterator>`. Because `cont_type` is a Boost.Intrusive set or list (both of which provide bidirectional iterators), all four iterator flavors — `iterator`, `const_iterator`, `reverse_iterator`, and `const_reverse_iterator` — are bidirectional. The chronological list iterators also satisfy bidirectionality, enabling `rbegin()`/`rend()` traversal from newest to oldest element without any additional cost.