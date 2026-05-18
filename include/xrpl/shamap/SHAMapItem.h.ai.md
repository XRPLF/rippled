# `SHAMapItem.h` — Leaf Data Items for the SHAMap Trie

`SHAMapItem` is the payload-bearing leaf object in the XRP Ledger's Merkle-Patricia trie (`SHAMap`). Every ledger object, transaction, or transaction-with-metadata that participates in consensus state is ultimately stored as a `SHAMapItem` keyed by its `uint256` hash. The file is entirely self-contained: it defines the class, the slab allocator pool that backs it, and the `boost::intrusive_ptr` lifetime hooks — all in the header.

## Variable-Length Struct Layout

The central design decision in `SHAMapItem` is the **struct-hack layout**: the item's payload bytes are placed in memory directly after the fixed-size struct fields. The constructor does this with a single `std::memcpy` into `reinterpret_cast<uint8_t*>(this) + sizeof(*this)`, and `data()` reads it back the same way. This avoids any separate heap allocation for the payload, keeping the header and payload in one contiguous block with one allocation lifetime.

Because the payload must immediately follow the struct, objects cannot be constructed on the stack or by value — the constructor is `private`, copy and move operations are all explicitly deleted, and the only valid creation path is the `make_shamapitem()` factory, which pre-allocates the right amount of raw memory before calling placement new. `size_` is stored as `uint32_t` rather than `size_t` to save four bytes of struct size (the comment notes that no SHAMap item will ever exceed 4 GB), which directly reduces the amount of dead space per slab slot.

## Slab Allocation Strategy

For a production XRPL node, hundreds of thousands of `SHAMapItem` objects are live simultaneously. Routing each allocation through the system allocator would fragment the heap and lose cache locality. Instead, the file declares a module-level `detail::slabber` — a `SlabAllocatorSet<SHAMapItem>` — with seven size tiers ranging from 128 to 1052 extra bytes (added to `sizeof(SHAMapItem)`):

```
128 B → 60 MiB   192 B → 46 MiB   272 B → 60 MiB
384 B → 56 MiB   564 B → 40 MiB   772 B → 46 MiB
1052 B → 60 MiB
```

The cutoffs and backing sizes were tuned to the empirical size distribution of ledger objects and to minimise intra-block padding. Each backing block is allocated at a 2 MiB boundary, allowing Linux's transparent huge-page support to engage automatically when available. Blocks are linked into a lock-free list per `SlabAllocator`; allocation within a block uses a per-block mutex only to pop from a freelist, keeping contention minimal.

`make_shamapitem()` calls `detail::slabber.allocate(data.size())`, which walks the sorted allocator list and returns from the first tier whose slot size fits `sizeof(SHAMapItem) + data.size()`. If all tiers are exhausted or the payload exceeds the largest tier, the factory falls back to `new uint8_t[sizeof(SHAMapItem) + data.size()]`. The maximum allowed payload is asserted to be ≤ 16 MiB, serving as a sanity guard against corrupt inputs.

## Intrusive Reference Counting

`SHAMapItem` is always managed through `boost::intrusive_ptr<SHAMapItem const>`, embedding the reference count inside the object as `mutable std::atomic<uint32_t> refcount_`. This avoids the separate control-block allocation that `std::shared_ptr` requires, which matters both for allocation overhead and for cache layout when many leaf nodes share the same item (copy-on-write in the trie creates sharing).

The ADL-found friends `intrusive_ptr_add_ref` and `intrusive_ptr_release` implement the protocol. `add_ref` guards against a pathological race: if the refcount has already reached zero before the increment, it calls `LogicError` rather than silently resurrecting a dead object. The constructor initialises `refcount_` to `1`, so `make_shamapitem` passes `false` as the second argument to `boost::intrusive_ptr{ptr, false}` — explicitly suppressing the automatic increment that would otherwise bring the count to 2.

`intrusive_ptr_release` handles the two-phase destruction that the layout demands: it first calls `std::destroy_at` to properly run the `SHAMapItem` destructor (needed because `CountedObject`'s destructor is not trivial — it decrements a global diagnostic counter), then returns the raw memory to `detail::slabber`. If the pointer wasn't slab-managed (because it came from the `new uint8_t[]` fallback path), `slabber.deallocate` returns `false` and the code falls through to `delete[]`. The `if constexpr (!std::is_trivially_destructible_v<SHAMapItem>)` guard is forward-looking: if the destructor chain ever becomes trivial, the compile-time branch eliminates the call entirely.

## Immutability and Use in SHAMapLeafNode

`SHAMapLeafNode` holds `boost::intrusive_ptr<SHAMapItem const>` — the `const` is intentional. Once constructed, a `SHAMapItem`'s key and payload never change; the SHAMap copy-on-write protocol creates a new item rather than modifying an existing one. The duplicate `make_shamapitem(SHAMapItem const& other)` overload is the copy constructor the class itself refuses to provide, producing a freshly allocated, independently owned item with the same key and payload.

## Diagnostics and Alignment

`CountedObject<SHAMapItem>` contributes a global atomic counter accessible via `CountedObjects::getInstance()`, allowing operators to observe how many `SHAMapItem` objects are alive at any given time — useful for diagnosing memory growth or cache pressure. The `static_assert` at the bottom of the file confirms that `alignof(SHAMapItem)` is exactly 4 or 8 (never, say, 40), which is a prerequisite for the slab allocator's alignment contracts. This assert would catch platform-specific or compiler-version surprises before they silently corrupt memory.