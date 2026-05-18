# `TaggedPointer.h` — Sparse Child-Array Manager for SHAMap Inner Nodes

## Why This File Exists

A `SHAMapInnerNode` is a radix-trie node with up to 16 children (the `branchFactor`). In a dense representation, every inner node always allocates arrays of exactly 16 `SHAMapHash` values and 16 `SharedPtr<SHAMapTreeNode>` smart pointers — even when most slots are empty. Because a live XRP Ledger contains millions of these nodes, and empirical measurements show that most inner nodes hold only a handful of children, that dense layout is extremely wasteful. According to the class comment, the sparse representation cuts average inner-node memory to roughly 25% of the naive allocation.

`TaggedPointer` is the mechanism that makes this possible. It owns a single heap allocation large enough to hold a packed, contiguous pair of arrays — `SHAMapHash[]` immediately followed by `SharedPtr<SHAMapTreeNode>[]` — and encodes the size class of that allocation in the two lowest bits of the pointer itself. Callers never see raw pointers or raw sizes; they ask the tagged pointer for its arrays.

## The Pointer-Tag Trick

All modern allocators guarantee that the natural alignment of objects is a power of two. `SHAMapHash` is asserted to have an alignment of at least 4, which means the raw address returned by `malloc` / `pool::malloc` always has its two low bits set to zero. Those two bits are reclaimed as a 2-bit **tag** field. The single `uintptr_t` member `tp_` is therefore both a pointer and a small integer simultaneously:

```
tp_ = reinterpret_cast<uintptr_t>(rawPtr) | tag;
```

`decode()` separates them with a pair of masks (`tagMask = 0x3`, `ptrMask = ~tagMask`). `getHashesAndChildren()` calls `decode()`, casts the pointer bits back to `SHAMapHash*`, and computes the `SharedPtr` array start as `hashes + numAllocated`. The two arrays live in one contiguous allocation with no gap or padding between them.

A moved-from `TaggedPointer` leaves `tp_ = 0`, which `destroyHashesAndChildren()` treats as a sentinel meaning "nothing to free."

## Size Classes and Pool Allocation

The tag indexes into a compile-time array defined in `TaggedPointer.ipp`:

```cpp
constexpr std::array<std::uint8_t, 4> boundaries{2, 4, 6, 16};
```

Two bits yield exactly four values (tags 0–3), so the design is complete by construction. Tag 3 (`boundaries[3] == 16`) is the **dense** case — all 16 slots exist; `isDense()` checks for this. Tags 0–2 are **sparse** cases with 2, 4, or 6 slots, sized by rounding the requested child count up via `std::lower_bound` on `boundaries`. Because the tag is constrained to `boundaries.size() - 1`, the `static_assert` in the `.ipp` file enforces that nobody enlarges the `boundaries` array past 4 entries without reconsidering the 2-bit encoding.

Each of the four size classes is backed by its own `boost::singleton_pool`, with 512 KB blocks and a `std::mutex` for thread safety. This avoids the fragmentation and overhead of general-purpose `new`/`delete` for these hot, fixed-size allocations. The pools for allocating and deallocating are captured as `std::function` arrays (`allocateArrayFuns`, `freeArrayFuns`, `isFromArrayFuns`) indexed by tag value, keeping dispatch O(1) and branch-free.

## Sparse vs. Dense Layouts

In the **dense** layout (tag 3), array index equals child branch number directly — child 7 is always at array index 7. In a **sparse** layout, only the non-empty children are stored, packed together in branch-number order. The owning `SHAMapInnerNode` maintains a separate `uint16_t isBranch_` bitset where bit `i` is set when branch `i` is non-empty.

Translating a branch number to a sparse array index is the job of `getChildIndex(isBranch, i)`:

```cpp
auto const mask = (1u << i) - 1;
return popcnt16(isBranch & mask);
```

This is a single popcount on the `isBranch` bits below position `i`. It is both correct and fast — the array position of child `i` is exactly the count of non-empty children that precede it.

The two iteration helpers `iterChildren` and `iterNonEmptyChildIndexes` branch once on `isDense()` and then loop. For the dense case the callback receives a direct array index; for the sparse case the callback receives both the logical branch number and the compact array index, which the caller needs to index into the hash/children arrays.

## Constructor Hierarchy and the `RawAllocateTag` Pattern

`TaggedPointer` is move-only (copy is deleted). Three public constructors cover the common lifecycle events:

- **`TaggedPointer(uint8_t numChildren)`** — normal creation. Rounds up `numChildren` to the nearest boundary, allocates, and runs default-constructors on all slots. Used when a new inner node is born.

- **`TaggedPointer(TaggedPointer&& other, uint16_t isBranch, uint8_t toAllocate)`** — resize. Used by `SHAMapInnerNode::resizeChildArrays()` when a child is added or removed and the array needs to grow or shrink. It moves the old `TaggedPointer` in and, if the requested size class is the same, returns immediately. Otherwise it allocates a new block, copies/moves entries using `iterNonEmptyChildIndexes`, then runs default constructors on the remaining slots.

- **`TaggedPointer(TaggedPointer&& other, uint16_t srcBranches, uint16_t dstBranches, uint8_t toAllocate)`** — the most general form. Handles simultaneous resize and set-difference on the branch bitsets (e.g., when an element is removed and the representation is compacted). When the old and new size classes match it operates **in-place**, shifting elements left or right inside the existing allocation. When they differ it allocates a new block and copies elements across with placement-new.

The private **`TaggedPointer(RawAllocateTag, uint8_t numChildren)`** constructor allocates the chunk from the pool and encodes the tag but deliberately skips running constructors on the `SHAMapHash` and `SharedPtr` elements. This is safe only because every callsite immediately follows it with placement-new loops. The `RawAllocateTag` tag type is an empty struct used solely to select this overload, making the intent visible and preventing accidental misuse. The destructor always calls `destroyHashesAndChildren()`, which calls the explicit destructors for all `numAllocated` elements, so if the constructors are never run the destructor would be UB — hence the strict encapsulation.

## The `popcnt16` Free Function

Defined at file scope, `popcnt16` counts the set bits of a 16-bit value. It dispatches to `std::popcount` (C++20), `__builtin_popcount` (GCC/Clang), or a compile-time-generated lookup table as a fallback. It appears in `getChildIndex` and in `SHAMapInnerNode::getBranchCount()` (which calls `popcnt16(isBranch_)`) — both hot paths during trie traversal.

## Relationship to `SHAMapInnerNode`

`SHAMapInnerNode` holds a single `TaggedPointer hashesAndChildren_` member. Every public operation on children delegates to it, passing `isBranch_` as context. The `isBranch_` bitset is separate from `TaggedPointer` by design: the tagged pointer owns memory, not logical state. This separation keeps the two concerns orthogonal and allows the bitset to be read atomically (behind the node's `lock_` spinlock) without touching the allocation.

The `.ipp` file rather than a `.cpp` file is used for the implementation because the two template methods (`iterChildren`, `iterNonEmptyChildIndexes`) must be instantiated in `SHAMapInnerNode.cpp`, which `#include`s the `.ipp` directly. This avoids explicit instantiation declarations while keeping the template bodies out of the header.