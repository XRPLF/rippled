# `TaggedPointer.ipp` — Sparse Child Storage for SHAMap Inner Nodes

## Purpose and Context

`TaggedPointer.ipp` provides the complete implementation of `TaggedPointer`, a low-level memory optimization used exclusively by `SHAMapInnerNode`. Every inner node in the XRPL SHAMap radix tree potentially has 16 child slots (`branchFactor = 16`), but in practice most nodes are sparsely populated. Allocating 16 `SHAMapHash` values and 16 `SharedPtr<SHAMapTreeNode>` pointers per node wastes large amounts of RAM when only 1 or 2 children actually exist. `TaggedPointer` solves this by dynamically choosing between four allocation sizes — {2, 4, 6, 16} child slots — and encoding the chosen size directly inside the pointer's unused low bits.

## The Tagged Pointer Layout

Modern platforms guarantee that objects with alignment ≥ 4 have their lower two address bits always zero. The `SHAMapHash` type is `static_assert`-verified to have `alignof >= 4`, so a raw pointer to it always has `tp_ & 3 == 0`. `TaggedPointer` repurposes those two bits as a tag — an index into the `boundaries` array `{2, 4, 6, 16}`. Decoding is trivial: `tp_ & tagMask` yields the tag, and `tp_ & ptrMask` recovers the raw pointer.

The tag directly tells `capacity()` how large the arrays are: `boundaries[tag]`. Tag 3 (the last slot, mapping to 16) identifies the *dense* representation; tags 0–2 identify *sparse* representations. `isDense()` is therefore just `(tp_ & tagMask) == 3`.

The pointed-to memory holds two co-located arrays: `N` instances of `SHAMapHash` followed immediately by `N` instances of `SharedPtr<SHAMapTreeNode>`, where `N = boundaries[tag]`. `getHashesAndChildren()` decodes the tag, computes the hash array start as a plain cast of the pointer, then advances by `N * sizeof(SHAMapHash)` to find the children array.

## Pool Allocation Strategy

Rather than calling `operator new` per node, the implementation uses four separate `boost::singleton_pool` instances — one for each boundary size. Each pool is templated on its exact chunk size (`boundaries[I] * elementSizeBytes`), where `elementSizeBytes` is `sizeof(SHAMapHash) + sizeof(SharedPtr<SHAMapTreeNode>)`. Pools are configured with 512 KiB blocks subdivided into as many fixed-size chunks as fit.

The pool interface is materialized at startup into three `std::array<std::function<...>, 4>` globals — `allocateArrayFuns`, `freeArrayFuns`, and `isFromArrayFuns` — using an `std::index_sequence` parameter pack expansion. This allows the allocator to dispatch by boundary index with a single array lookup at runtime, rather than a `switch` statement or virtual dispatch. The `isFromArrayFuns` array is used defensively in `deallocateArrays` to assert that memory is returned to the correct pool.

This design matters for performance: SHAMap operations can create and destroy millions of inner nodes during ledger validation. Pool allocation gives O(1) fixed-overhead allocation and avoids heap fragmentation entirely, since all chunks of a given size are managed in a single segregated free list.

## Sparse vs. Dense Representation

In sparse mode, only the non-empty children are stored, packed in branch-index order. The `isBranch_` bitfield (a 16-bit integer on `SHAMapInnerNode`) tracks which branches are populated. Translating a branch index `i` into a sparse array index is done via `getChildIndex()`: it computes `popcnt16(isBranch & ((1 << i) - 1))` — the count of set bits below position `i` in the bitset equals the number of occupied slots before slot `i` in the sorted sparse array.

`iterChildren()` exposes all 16 logical branches to its callback regardless of representation. In sparse mode it walks the bit positions of `isBranch`, emitting real hashes for set bits and the static `zeroSHAMapHash` singleton for unset bits. `iterNonEmptyChildIndexes()` gives both the branch number and the array index to its callback, letting callers work with either coordinate system.

## Constructor Design and the In-Place Optimization

`TaggedPointer` is move-only (copy is deleted). The most architecturally interesting part is its pair of restructuring constructors, used when `SHAMapInnerNode` needs to resize or reshape its child arrays.

The three-argument constructor `TaggedPointer(TaggedPointer&&, isBranch, toAllocate)` resizes the arrays to fit `toAllocate` children. If the source already has the right capacity, it performs the conversion *in-place* using `iterNonEmptyChildIndexes` and placement moves. Otherwise it allocates a new chunk, moves children over, and destroys the old storage.

The four-argument constructor `TaggedPointer(TaggedPointer&&, srcBranches, dstBranches, toAllocate)` handles a more general case: the caller specifies which children exist in the source (`srcBranches`) and which should exist in the destination (`dstBranches`). This is used when simultaneously changing the child set and the representation. The in-place path (same capacity) iterates all 16 branch positions and performs left/right shifts within the sparse array for insertions and deletions. The out-of-place path placement-constructs elements into the new allocation. Both paths handle all four intersection cases: kept (`inSrc && inDst`), removed (`inSrc && !inDst`), added (`!inSrc && inDst`), and absent from both.

The private `RawAllocateTag` constructor variant allocates raw memory from a pool without running any constructors. It is used only internally, in pairs with explicit placement-new loops — a necessary pattern because `destroyHashesAndChildren()` always runs explicit destructors and must not be called on unconstructed objects.

## Destruction and Invariants

`destroyHashesAndChildren()` manually destructs each `SHAMapHash` and `SharedPtr<SHAMapTreeNode>` element before returning the chunk to its pool. This is mandatory because the objects were created with placement new into pool-allocated memory that the C++ runtime does not track. Move-from sets `tp_ = 0`, and the null check `if (!tp_) return` at the top of `destroyHashesAndChildren()` makes moved-from objects safely destructible.

Two compile-time constraints guard the entire design: `boundaries.size() <= 4` (the tag field is exactly 2 bits, supporting at most 4 values) and `boundaries.back() == branchFactor` (the last boundary must be the full dense count, so the dense case is unambiguously the final tag value). If either constraint is violated — say, someone adds a fifth boundary — the build fails immediately.