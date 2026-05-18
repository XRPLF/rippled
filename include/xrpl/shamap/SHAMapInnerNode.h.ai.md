# `SHAMapInnerNode.h` — Inner Node of the XRPL Merkle Radix Tree

## Role in the System

`SHAMapInnerNode` is the branching (non-leaf) node type of the SHAMap, the authenticated Merkle radix tree used by XRPL to represent all ledger state and transaction sets. Each inner node fans out into exactly 16 children (one per hexadecimal nibble of a 256-bit key), making the tree depth at most 64 levels. The class inherits from `SHAMapTreeNode`, which provides the copy-on-write identity (`cowid_`) and hash storage, and from `CountedObject<SHAMapInnerNode>` for per-type allocation tracking.

## Sparse Child Storage via `TaggedPointer`

The most significant architectural decision in this file is the use of `TaggedPointer hashesAndChildren_` to hold both the child hashes and child node pointers in a single pointer-sized field. `TaggedPointer` exploits the fact that pointers are naturally aligned to at least 4 bytes, so the two lowest bits of the pointer are always zero at runtime. Those bits store a tag (0–3) that encodes which of four capacity tiers the arrays use: 2, 4, 8, or 16 elements. The hash array and the shared-pointer array are laid out back-to-back in a single allocation.

The `isBranch_` field (`uint16_t`, one bit per branch) is the authoritative record of which of the 16 branches are populated. When the arrays are smaller than 16 elements (sparse mode), children are packed in branch-index order: if only branches 2 and 14 are occupied, they sit at array positions 0 and 1 respectively. `getChildIndex(int i)` converts from logical branch number to physical array index; it returns `std::nullopt` when the branch is empty and the arrays are sparse. `iterNonEmptyChildIndexes(F)` supplies both the branch number and the array index to its callback, bridging the logical and physical views.

The motivation is RAM. A production SHAMap has a large proportion of inner nodes that hold only a handful of children. Measurements cited in `TaggedPointer.h` show that sparse representation reduces inner-node memory to roughly 25% of the naive dense representation. `resizeChildArrays()` handles the lifecycle transition: when a child is added or removed, it reconstructs `hashesAndChildren_` via the `TaggedPointer` move constructor that accepts both source and destination branch bitsets, copying only the surviving children.

## Copy-on-Write and `clone()`

The `cowid_` field inherited from `SHAMapTreeNode` identifies the SHAMap that exclusively owns this node. A value of 0 signals that the node is clean and shareable across multiple map instances (snapshots, parallel reads). Any SHAMap that needs to modify a shared node must call `clone()` first, which allocates a new `SHAMapInnerNode` with the caller's `cowid` and copies all hashes and child pointers.

The `clone()` implementation is careful about the sparse/dense distinction. If the source is sparse, it re-packs child hashes into sequential positions in the clone's arrays. It acquires the per-node `lock_` spinlock only for the child-pointer copy — hashes can be read without locking because they are immutable on shared nodes.

## Fine-Grained Bit-Level Spinlocking

`lock_` is a `mutable std::atomic<uint16_t>` used as a 16-bit lock with one bit per child slot, not per node. `getChild()`, `getChildPointer()`, and `canonicalizeChild()` all use a `packed_spinlock` that spins on the single bit corresponding to the child's physical array index. This allows concurrent access to different children of the same node without global serialization.

`canonicalizeChild()` is the deduplication primitive used when multiple threads simultaneously load the same child from backing storage. The first caller to lock the child's bit slot installs its freshly-loaded node pointer; any subsequent caller finds the slot already populated and returns the existing pointer instead — this is the "winner keeps it" pattern for concurrent lazy loading. The node hash is verified to match before installation as a consistency check.

## Full-Below Optimization

`fullBelowGen_` stores a generation counter. When `isFullBelow(generation)` returns true, the caller knows that every node in the entire subtree below this node is already present in local storage and does not need to be fetched from peers. `setFullBelowGen(gen)` marks the subtree as complete for the current synchronization pass. Because generations monotonically increase, a stale `fullBelowGen_` value automatically becomes invalid on the next sync cycle without requiring explicit invalidation.

## Wire Serialization

`serializeForWire()` chooses between two wire formats based on occupancy. Nodes with fewer than 12 populated branches use the *compressed inner* format: each non-empty branch is emitted as a 256-bit hash followed by a one-byte position, for 33 bytes per child. Denser nodes use the *full inner* format: all 16 hashes in order, 512 bytes total. The type byte appended at the end lets the receiver decode the format. `makeFullInner()` and `makeCompressedInner()` are the static factory methods that reconstruct an inner node from these respective wire formats; both call `resizeChildArrays()` after parsing to right-size the arrays for actual occupancy.

`serializeWithPrefix()` always emits the full 16-hash form preceded by the `HashPrefix::innerNode` type prefix. This is the canonical hash-input form used by `updateHash()`.

## Hash Computation

`updateHash()` produces the node's Merkle hash as SHA-512/2 of `HashPrefix::innerNode` concatenated with all 16 child hashes, feeding zero-hashes for empty branches. The result is the commitment covering the entire subtree. `updateHashDeep()` first pulls hashes from in-memory child objects into the local `hashes` array (bridging the case where child nodes were set via pointer but `hashes` was zeroed by `setChild()`), then delegates to `updateHash()`.

## Destruction and Intrusive Weak Pointers

`partialDestructor()` is called by the intrusive reference-count infrastructure before memory is reclaimed but while weak references to the object may still exist. It explicitly resets all child `SharedPtr`s, breaking any reference cycles and ensuring clean resource teardown even when the object's storage outlives its strong references.