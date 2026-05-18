# `aged_unordered_container.h` — Time-Indexed Hash Container

## Role in the System

This file provides `aged_unordered_container`, the implementation backbone for all four of XRPL's beast hash-based aged containers: `aged_unordered_set`, `aged_unordered_multiset`, `aged_unordered_map`, and `aged_unordered_multimap`. Its purpose is to give callers the O(1) lookup semantics of a standard `std::unordered_map` while simultaneously maintaining every element's insertion or last-touch timestamp in chronological order. This combination is the fundamental primitive for building time-bounded caches in the XRP Ledger — entries can be inserted, found by key in constant time, and expired in bulk by walking the chronological front of the container.

The template signature encodes four orthogonal degrees of freedom:

```cpp
template <bool IsMulti, bool IsMap, class Key, class T,
          class Clock, class Hash, class KeyEqual, class Allocator>
class aged_unordered_container
```

`IsMulti` and `IsMap` toggle whether duplicate keys are permitted and whether the container is a map or set respectively, and `std::enable_if` guards on these flags select the correct overload family for every public mutating operation. The actual concrete-type aliases (`aged_unordered_map`, etc.) are thin wrappers that fix these two flags.

## Dual-Index Architecture

The central design decision is to use **two Boost.Intrusive containers simultaneously over the same pool of elements**. The private `element` struct multiply-inherits from both `boost::intrusive::unordered_set_base_hook` and `boost::intrusive::list_base_hook`:

```cpp
struct element : boost::intrusive::unordered_set_base_hook<...>,
                 boost::intrusive::list_base_hook<...>
{
    value_type value;
    time_point when;
};
```

This single node participates in two separate intrusive data structures without any extra allocation. The `m_cont` member is the hash container (unordered_set or unordered_multiset depending on `IsMulti`) used for O(1) key-based lookup, while `chronological.list` is an intrusive doubly-linked list kept in insertion/touch order. Every insert pushes the new element to the back of `chronological.list`; every `touch()` unlinks the element and splices it back onto the tail. The invariant is that `chronological.list.begin()` always points to the oldest element — exactly what the `expire()` free function exploits.

This architecture is far more efficient than maintaining a separate side-structure because there is zero extra heap allocation per element and zero pointer-indirection overhead; the hash container and the list share the exact same `element` objects.

## The `element` Node and Key Extraction

`element` stashes its container-level type aliases in a nested `stashed` struct. This is a deliberate forward-declaration trick: `aged_container_iterator` needs `value_type` and `time_point` without seeing the full container class, so it picks them up from `Iterator::value_type::stashed`. This breaks the mutual dependency between iterator and container template instantiations.

The `aged_associative_container_extract_t<IsMap>` functor provides the `extract()` helper. For maps it returns `value.first`; for sets it returns the value itself. This keeps the hash and equality functors uniform regardless of whether the contained type is a bare key or a key-value pair.

## Policy Consolidation: `config_t` and `empty_base_optimization`

All per-container policies — `ValueHash`, `KeyValueEqual`, and the element allocator — are packed into a single `config_t` object using private inheritance and `empty_base_optimization`. When these policy types (the common `std::hash<Key>`, `std::equal_to<Key>`, and `std::allocator`) are stateless, the compiler can apply the empty base optimization and `config_t` carries zero bytes of overhead for them.

`ValueHash` wraps the user-supplied `Hash` so that Boost.Intrusive, which operates on `element` nodes, can hash by extracting the key first. Similarly, `KeyValueEqual` wraps `KeyEqual` and provides three overloads — `(Key, element)`, `(element, Key)`, and `(element, element)` — so that Boost.Intrusive's heterogeneous lookup paths (`find`, `insert_check`, `count`) can compare a raw key against an element without constructing a temporary `element`.

The clock is stored as `std::reference_wrapper<clock_type>` inside `config_t`, reflecting that the container never owns the clock and the clock outlives the container. Using `abstract_clock<Clock>` rather than `Clock` directly means the container's clock can be replaced with a mock during unit tests — a dependency injection seam baked into the type system.

## Bucket Management and Rehashing

The `Buckets` inner class owns a `std::vector<bucket_type>` and the `max_load_factor` float. Boost.Intrusive's unordered containers require an external array of bucket objects supplied at construction time via `bucket_traits`. When the vector needs to grow, `Buckets::rehash()` must take care with the ordering of operations: if the vector's capacity is insufficient, it first swaps in a fresh vector to avoid reusing still-live bucket storage, then calls `c.rehash()` on the intrusive container, then resizes. When shrinking, the sequence is reversed: intrusive rehash first, vector resize second, again to avoid destroying non-empty buckets.

`maybe_rehash(n)` is called before every insert. It checks `would_exceed`, i.e., whether adding `n` items would push the load factor beyond the maximum, and if so resizes to accommodate `size() + n`. Bulk inserts from a pair of random-access iterators pre-batch the load check by measuring the distance in advance and calling `maybe_rehash` once, then delegating to `insert_unchecked` which skips the per-element check.

## Chronological Memberspace

The public `chronological` member of type `chronological_t` is a "memberspace" — an idiom for giving a class an inner namespace-like scope with its own iterator types and `begin`/`end`/`rbegin`/`rend`. The list backing `chronological` is declared `mutable` because `const` container operations still need to read it via const references.

`chronological_t::iterator_to(value_type&)` is noteworthy: it recovers the enclosing `element*` from a raw `value_type&` via pointer arithmetic using `offsetof`-style calculation with `addressof`. This is sound only because `element` is guaranteed to be standard-layout, enforced at the call site by `static_assert(std::is_standard_layout<element>::value, ...)`. The same `iterator_to` pattern appears on the main `m_cont` side.

## `touch()` and the `expire()` Free Function

`touch(pos)` resets an element's `when` to `clock().now()`, unlinks it from `chronological.list`, and splices it to the tail. This O(1) operation is the core of any LRU cache policy built on top of this container. `touch(key)` calls `equal_range` and touches all matching elements, returning the count; for a multimap use-case this lets all elements sharing a key be refreshed in one call.

The file-scope free function `expire(c, age)` is the canonical expiry loop:

```cpp
auto const expired(c.clock().now() - age);
for (auto iter(c.chronological.cbegin());
     iter != c.chronological.cend() && iter.when() <= expired;)
{
    iter = c.erase(iter);
    ++n;
}
```

Because the list is maintained in chronological order and `touch` always moves elements to the tail, `cbegin()` always points at the oldest entry. The loop terminates as soon as it encounters an element younger than the cutoff, making bulk expiry O(k) where k is the number of expired elements — no full scan required.

## Insert and Emplace Implementation

For non-multi containers, insert uses Boost.Intrusive's two-phase `insert_check` / `insert_commit` protocol: first check whether the key already exists (and capture where to insert if not), then — only if absent — allocate and construct the element and commit it. This avoids an unnecessary allocation on duplicate keys. For multi containers the check is skipped and the element is always inserted.

The `emplace` path for non-multi containers deviates from this in one place — the active `#if 1` block constructs the element before the uniqueness check and then deletes it if the key already exists. A commented-out `#else` block shows the `insert_check` / `insert_commit` alternative. The comment acknowledges this as unfortunate, and the dead code serves as a record of the considered-but-rejected alternative.

## Comparison Operators

`operator==` for non-multi containers iterates one container and checks membership in the other, deliberately comparing only keys (not `when` timestamps). The multi variant uses `equal_range` + `std::is_permutation` to handle repeated keys. The file guards against C++14's 4-iterator `is_permutation` via `BEAST_NO_CXX14_IS_PERMUTATION`, falling back to the 3-iterator version with a manual distance check as a workaround.