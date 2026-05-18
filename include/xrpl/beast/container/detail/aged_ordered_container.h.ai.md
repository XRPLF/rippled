# `aged_ordered_container.h` — Time-Indexed Associative Container

## Role and Purpose

`aged_ordered_container` is the single backing implementation for the four public type aliases in the beast container library: `aged_map`, `aged_set`, `aged_multimap`, and `aged_multiset`. It lives in `beast::detail` and is not used directly — callers use the thin alias headers (e.g., `aged_map.h`) which forward all template parameters to this class.

The container solves a problem that appears repeatedly in the XRPL node implementation: you need an associative lookup structure (find by key in O(log n)) while simultaneously being able to enumerate elements in the order they were last accessed (oldest first), so that expiration and cache eviction can be implemented without scanning the entire container. A plain `std::map` gives you the first axis; a plain `std::list` gives you the second; `aged_ordered_container` fuses both at the cost of a single extra pointer pair per element and no additional heap allocation.

## Dual-Index Architecture with Boost.Intrusive

The core design insight is that every `element` node participates in **two** independent intrusive data structures simultaneously. `element` inherits both `boost::intrusive::set_base_hook` and `boost::intrusive::list_base_hook`, embedding the BST node links and the doubly-linked-list node links directly into the same heap allocation:

```cpp
struct element : boost::intrusive::set_base_hook<...>,
                 boost::intrusive::list_base_hook<...> {
    value_type value;
    time_point when;
};
```

`m_cont` is a `boost::intrusive::set` (or `multiset`) that provides key-ordered access and O(log n) lookup. `chronological.list` is a `boost::intrusive::list` that holds the same nodes in insertion/touch order. Because both containers hold pointers into the same `element` objects, there is no duplication of the user data — the two indexes are pure overhead (four pointers per element: BST left/right/parent and list prev/next, collapsed by the intrusive base hooks into the element itself).

## The Chronological "Memberspace"

The `chronological_t` class is an instance of the *memberspace* idiom (cited inline, referencing an ACCU article). Rather than a free namespace or a base class, it is a non-copyable member object whose only purpose is to expose a distinct set of `begin()`/`end()`/`rbegin()`/`rend()` iterators over `chronological.list`. Code iterating elements from oldest to newest writes `container.chronological.begin()`, keeping both traversal dimensions syntactically visible on a single container object.

`chronological_t` is non-copyable and non-moveable, and is always a subobject of `aged_ordered_container` — the list it wraps (`list` member) is declared `mutable` so `const` iterators can still be obtained on a `const` container.

## `touch()` — Updating the Time Index

`touch(pos)` updates an element's `when` timestamp to `clock().now()`, then **unlinks the node from the chronological list and re-inserts it at the back**. The key-ordered index (`m_cont`) is unaffected. This O(1) operation moves any accessed element to the "newest" end of the list, making the front of the list permanently the oldest element. The public `touch(K const& k)` overload finds all matching elements via `equal_range` and calls the iterator form for each.

Both `touch` and `erase` use `is_boost_reverse_iterator<Iterator>` to statically reject reverse iterators through SFINAE: calling `erase(reverse_iterator)` is a compile error, not a runtime error. Boost.Intrusive's reverse iterators carry different hook types, and this trait detects them.

## `expire()` — Bulk Expiration

The free function `expire(c, age)` exploits the chronological ordering directly:

```cpp
auto const expired(c.clock().now() - age);
for (auto iter(c.chronological.cbegin());
     iter != c.chronological.cend() && iter.when() <= expired;)
    iter = c.erase(iter);
```

Because elements are ordered oldest-first in the list, the function only ever touches elements it removes. Once it encounters an element newer than the expiry threshold, the loop terminates in O(k) where k is the number of expired items — not O(n) over the whole container. The `aged_container_iterator` wrapper exposes `when()` directly on the iterator, which reads `m_iter->when` without an extra lookup.

## `element` Memory and `iterator_to`

`new_element` allocates a single `element` via `ElementAllocatorTraits::allocate`, constructs it with the current `clock().now()` timestamp, and uses a `unique_ptr` with a custom `Deleter` to guarantee the allocation is freed on exception. `delete_element` destroys and deallocates in reverse order.

`iterator_to(value_type&)` converts a reference to the user-visible `value` field back into a full `element*` via manual pointer arithmetic:

```cpp
reinterpret_cast<element*>(reinterpret_cast<uint8_t*>(&value) -
    ((std::size_t)std::addressof(((element*)0)->member)));
```

A `static_assert(std::is_standard_layout<element>::value)` guards this operation. Because `element` inherits from two hook base classes before declaring `value`, the offset of `value` is not zero and the arithmetic is necessary. This same pattern appears in both the key-ordered and chronological `iterator_to` overloads.

## `config_t` — Bundled Allocator, Comparator, Clock

Rather than storing the comparator, allocator, and clock as three separate fields, the private `config_t` class packs them together using two optimizations:

1. It inherits (privately) from `KeyValueCompare`, so a stateless comparator (the common case) occupies zero bytes via the empty base.
2. It inherits from `empty_base_optimization<ElementAllocator>`, which is itself a conditional private base/member depending on whether the allocator is empty and non-final. This is the same pattern used throughout Boost containers.
3. The clock is held as a `std::reference_wrapper<clock_type>`, ensuring the container never copies the clock object and always goes through the abstract interface.

## `emplace` Upfront Construction Cost

For non-multi containers, `emplace` must construct the candidate `element` before checking uniqueness in `m_cont`, because the key is buried inside the constructed value and there is no way to extract it without construction. If `insert_check` reports that the key already exists, the freshly constructed element is immediately destroyed via `delete_element`. The comment marks this with `VFALCO NOTE Its unfortunate that we need to construct element here`. The multi-container variant avoids this waste since duplicate keys are always accepted.

## Copy vs. Move Construction

The copy constructor re-inserts all elements through the normal `insert` path, stamping each with the current clock time rather than preserving the original timestamps — there is no way to copy the `time_point` values. The move constructor transfers `m_cont` and `chronological.list` directly via intrusive container move semantics, preserving timestamps intact. The unusual case — move construction with a differing allocator — falls back to element-by-element insertion followed by `other.clear()`, since the underlying nodes cannot be transferred across allocators.

## Public Interface Relationship

The equality operator notes explicitly that comparison is done only on keys, ignoring mapped values. This differs from `std::map::operator==` which compares both. The specialization of `is_aged_container` at file scope tags `aged_ordered_container` for trait-based dispatch elsewhere in the XRPL codebase.