# `SlabAllocator.h` — Fixed-Size Slab Memory Allocator

## Why This File Exists

Standard `malloc`/`new` carries costs that matter at XRPL's throughput: lock contention inside the system allocator, per-object bookkeeping overhead, and heap fragmentation that degrades TLB efficiency over time. `SlabAllocator.h` replaces this with a classic slab strategy: carve a large pre-aligned region into uniform slots, maintain a per-block free list, and serve allocations in O(1) without touching the system heap. The primary consumer is `SHAMapItem` — a high-churn node type that pairs a fixed header with a variable-length data payload living immediately after it in memory.

## `SlabAllocator<Type>`: Structure and Internals

### `SlabBlock` — Self-Hosting Memory Region

The central internal structure is `SlabBlock`. Each slab is a single `boost::alignment::aligned_alloc` call; the `SlabBlock` header is placement-new'd at the very start of that buffer, and the item pool occupies the rest. This self-hosting layout means only one system allocation is needed per slab regardless of how many objects it holds.

The constructor walks the entire pool and links every item-sized slot into a singly-linked free list. The link pointer is written with `std::memcpy` rather than a direct pointer cast:

```cpp
std::memcpy(data, &l_, sizeof(std::uint8_t*));
```

This idiom appears three times in the file. It is not premature caution — writing through a `uint8_t*` to store a `uint8_t**` value violates strict aliasing rules, which is undefined behavior that optimizers can miscompile in subtle ways. `memcpy` is the standard-blessed type-pun that compilers reliably lower to a plain store.

Each `SlabBlock` carries its own `std::mutex` protecting only its own free list (`l_`). Per-block rather than per-allocator locking is deliberate: under concurrent load, multiple threads can allocate from different slabs simultaneously without contending. The `own()` method is a pointer range check against `[p_, p_ + size_)` — O(1) and branch-predictor-friendly, relying on the pool's contiguity.

### Lock-Free Slab Growth

The set of active `SlabBlock` instances forms a lock-free singly-linked list through `std::atomic<SlabBlock*> slabs_`. When every existing slab is exhausted, `allocate()` allocates a fresh buffer at a **2 MiB boundary** — not an aesthetic choice, but a deliberate alignment to enable Linux transparent huge pages. For allocations ≥ 4 MiB, a `madvise(buf, size, MADV_HUGEPAGE)` hint is issued on Linux, potentially reducing TLB pressure significantly under memory-intensive workloads.

The new slab is linked with a `compare_exchange_weak` CAS loop:

```cpp
while (!slabs_.compare_exchange_weak(
    slab->next_, slab, std::memory_order_release, std::memory_order_relaxed))
    ;
```

This is the standard lock-free list prepend. A subtle consequence: two threads could concurrently decide that all existing slabs are exhausted, both allocate new slabs, and both successfully link them. The result is a wasted slab's worth of memory in that rare race. The design explicitly accepts this for the sake of eliminating a global growth lock — correct, since slab growth events are infrequent and slab memory is not small.

### The Intentional Destructor Leak

The destructor body is empty with a `FIXME` comment explaining that releasing slab memory at shutdown is unsafe: there is no mechanism to guarantee that objects constructed inside the slab have been destroyed first. XRPL's shutdown model does not provide this guarantee, so the destructor deliberately leaks all slab memory. A controlled leak is far safer than a use-after-free.

### Constructor Parameters and Disabled Allocators

`SlabAllocator(extra, alloc, align)` accepts:
- `extra`: additional bytes beyond `sizeof(Type)` per slot (for trailing payload)
- `alloc`: slab size in bytes; **0 means the allocator is permanently disabled** and will always return `nullptr` — an explicit design affordance for environments needing minimal memory usage
- `align`: override for per-slot alignment; defaults to `alignof(Type)`

`itemSize_` is computed as `align_up(sizeof(Type) + extra, itemAlignment_)`, ensuring every slot satisfies the type's alignment requirements including any trailing data.

Two `static_assert`s enforce hard constraints: `sizeof(Type) >= sizeof(uint8_t*)` (the free-list pointer must fit inside the slot it inhabits), and `alignof(Type)` must be 4 or 8.

## `SlabAllocatorSet<Type>`: Tiered Dispatch

`SlabAllocatorSet` groups up to 64 `SlabAllocator<Type>` instances in a `boost::container::static_vector` — a fixed-capacity, stack-allocated container that avoids a heap allocation for the allocator array itself. Allocators are sorted by item size at construction and validated for uniqueness; duplicate sizes throw `std::runtime_error` at startup, appropriate since this is a static configuration error.

`allocate(extra)` performs a linear scan for the smallest slot that can fit `sizeof(Type) + extra`, short-circuiting through the `maxSize_` fast path:

```cpp
if (auto const size = sizeof(Type) + extra; size <= maxSize_)
```

If no configured tier can satisfy the request, `nullptr` is returned immediately without scanning. This lets callers implement a transparent fallback to `operator new[]` without coupling the allocator to any specific failure policy.

`deallocate(ptr)` iterates across allocators and relies on each `SlabAllocator::deallocate()` returning `bool` to indicate ownership. The return value propagates to the caller so it can distinguish "pointer owned by the slab" from "pointer was allocated some other way."

`SlabConfig` is a nested public value type exposing the `(extra, alloc, align)` triple, with `SlabAllocatorSet` declared as a `friend` to access its private fields. This keeps the construction API declarative without polluting the public interface with setters.

## How This Is Used: `SHAMapItem`

The concrete deployment is in `SHAMapItem.h`, where an `inline` global `SlabAllocatorSet<SHAMapItem>` named `slabber` is configured with seven size tiers:

```cpp
inline SlabAllocatorSet<SHAMapItem> slabber({
    {  128, megabytes(60) },
    {  192, megabytes(46) },
    {  272, megabytes(60) },
    {  384, megabytes(56) },
    {  564, megabytes(40) },
    {  772, megabytes(46) },
    { 1052, megabytes(60) },
});
```

The sizes and slab capacities are manually tuned to match the expected distribution of ledger object sizes and to minimize intra-slab slack. `make_shamapitem()` calls `slabber.allocate(data.size())`, falls back to `new std::uint8_t[sizeof(SHAMapItem) + data.size()]` if the slab can't satisfy the request, then placement-new's the `SHAMapItem` into the raw memory. The matching `intrusive_ptr_release()` calls `slabber.deallocate()`, falling back to `delete[]` if the pointer wasn't slab-owned. This opt-in, fallback-capable design means `SlabAllocator` never needs to handle arbitrarily sized allocations and the caller never hard-depends on the slab being able to serve the request.