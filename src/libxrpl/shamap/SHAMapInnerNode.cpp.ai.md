# `SHAMapInnerNode.cpp` — Inner Routing Node of the SHAMap Merkle Trie

## Role in the System

The XRP Ledger's state and transaction data are authenticated through the `SHAMap`, a 16-way radix Merkle trie. `SHAMapInnerNode` is the non-leaf (routing) node of that trie. Every inner node branches on exactly 4 bits of a 256-bit key path, giving a `branchFactor` of 16. Its cryptographic hash is deterministically derived from the hashes of its children, and the root hash of the entire trie is the canonical fingerprint of ledger state.

This file implements the logic needed to create, modify, clone, hash, serialize, and validate these inner nodes while satisfying the trie's concurrent-read, copy-on-write (CoW) modification model.

## Memory Layout: `TaggedPointer`

The most architecturally significant detail is `hashesAndChildren_`, a `TaggedPointer` that stores two parallel arrays — a `SHAMapHash[]` for known child hashes and an `intr_ptr::SharedPtr<SHAMapTreeNode>[]` for in-memory child pointers — in a *single* allocation, with the array capacity encoded in the lowest two bits of the pointer. `TaggedPointer` supports only four allocation sizes, and nodes typically start small (default `numAllocatedChildren = 2`).

This sparsity is the primary RAM optimization. A fully populated inner node needs space for 16 hash+pointer pairs; a node with two children needs only two. According to the header comment, this reduces average inner-node memory to roughly 25% of a naive dense layout. The `isBranch_` field is a 16-bit bitmask that maps logical branch positions to physical array indices, allowing iteration over non-empty branches at O(population-count) cost rather than O(16).

The dual-array layout is also the reason `iterChildren()` and `iterNonEmptyChildIndexes()` exist as separate primitives: hash computation always needs all 16 slots (zero-filled for absent children), while mutation and serialization only need non-empty slots.

## Copy-on-Write Semantics

`SHAMapTreeNode` carries a `cowid_` field: a non-zero value names the owning `SHAMap`; zero means the node is freely shared. All mutation methods assert `cowid_ != 0` (guarded by `XRPL_ASSERT`), making it a hard invariant that only the owning map modifies a node. When another map needs to modify a shared node, it calls `clone(cowid)`, which produces a deep copy with the new owner's id. 

The `clone()` implementation has a subtle correctness requirement: it copies hashes outside the lock (they are immutable once set) but copies child shared-pointer references under the per-child spinlock, because another thread might be racing to install a freshly-fetched child pointer at the same time.

## Per-Child Spinlocks

`lock_` is a `std::atomic<std::uint16_t>` used as a compact spinlock array: one bit per branch, 16 bits total. The `packed_spinlock` wrapper acquires exactly one of those bits via `fetch_or`/`fetch_and` with acquire/release ordering. This means reads of *different* children can proceed concurrently without serializing on a single mutex. Only `getChild()`, `getChildPointer()`, and `canonicalizeChild()` acquire these locks; `setChild()` and `shareChild()` do not, since they operate only on CoW-owned (non-shared) nodes and thus have exclusive access by construction.

The full-node `spinlock` (not `packed_spinlock`) is also present in `clone()`, where the entire children array needs to be snapshot consistently.

## `setChild()` — Dynamic Resizing

When a child is added to or removed from a node, the branch count changes and the `TaggedPointer` may need to be reallocated to a different capacity tier. `setChild()` computes `dstIsBranch`, derives the needed allocation count via `popcnt16`, and then constructs a new `TaggedPointer` in place, moving existing entries. This reallocation is handled entirely within the `TaggedPointer` constructors to avoid double-copying. After the resize, the child pointer is installed and `hash_` is zeroed, marking the node as dirty.

## `canonicalizeChild()` — First-Writer Wins

When a node is fetched from the database and installed into an inner node, multiple threads might simultaneously fetch the same child. `canonicalizeChild()` resolves the race under the per-child spinlock: if a child is already set, the incumbent wins and the caller's newly-constructed node is discarded. If the slot is empty, the caller's node is installed. This implements a lazy, lock-safe memoization pattern that ensures the entire tree converges on a single canonical object per node, avoiding duplicated in-memory representations.

## Hash Computation

`updateHash()` iterates all 16 branches via `iterChildren()` (including zero hashes for absent branches), prepends `HashPrefix::innerNode`, and computes the SHA-512/2 half-hash. Iterating all 16 branches regardless of sparsity is intentional: the hash must be identical whether the node is stored densely or sparsely. `updateHashDeep()` additionally pulls each child's current hash from the in-memory child pointer before calling `updateHash()`, which is used during trie construction or after batch mutations where child hashes may have been updated in memory but not yet propagated upward.

## Serialization: Dense vs. Compressed Wire Format

`serializeForWire()` applies a cutoff of 12 occupied branches. Nodes with fewer than 12 children use the *compressed inner* format: each non-empty branch is encoded as a 32-byte hash followed by a 1-byte branch index (33 bytes × n). Nodes with 12 or more branches use the *full inner* format: all 16 hashes in sequence (512 bytes). `makeFullInner()` and `makeCompressedInner()` are the corresponding deserialization factories, validated against exact size constraints and throwing `std::runtime_error` on malformed input. The compressed format is beneficial on the wire (typical inner nodes are sparse) but adds branch-index bookkeeping.

## `partialDestructor()` and Intrusive Weak Pointers

`SHAMapInnerNode` inherits from `IntrusiveRefCounts`, which supports weak pointers. When the strong reference count reaches zero but weak references are still live, `partialDestructor()` is called before the object is destroyed: it manually calls `.reset()` on every child `SharedPtr`, breaking reference cycles without releasing the object's memory. This is the canonical pattern for intrusive ref-count systems that need to separate resource release from deallocation.

## `invariants()`

The `invariants()` method is a debug-time coherence check. It verifies that the `isBranch_` bitmask is consistent with non-zero hashes, that non-root nodes have non-zero hashes, and that the `count` of non-empty branches matches hash zeroness. It also recurses into children. This is called during testing and debugging to catch corruption early, and it correctly handles both dense and sparse array layouts by branching on `numAllocated == branchFactor`.