# `include/xrpl/shamap/SHAMap.h`

## Role and Purpose

`SHAMap` is the foundational authenticated data structure in the XRP Ledger. Every ledger is composed of two `SHAMap` instances: one that maps transaction IDs to transaction data (type `TRANSACTION`), and one that maps account-state object keys to their serialized state (type `STATE`). The root hash of each tree is what a validator signs and what makes ledger agreement deterministic — two validators hold the same ledger if and only if their `SHAMap` root hashes match.

The class is simultaneously a radix tree and a Merkle tree. As a radix tree with fan-out 16, each inner node selects one of 16 children based on a 4-bit nibble of the 256-bit key, giving a fixed tree depth of 64 levels (`leafDepth = 64`; keys are consumed 4 bits per level across a 256-bit key space). As a Merkle tree, every inner node's hash is derived from the combined hashes of its children. This combination gives O(log N) key lookup and O(log N) membership proofs while allowing any subset of nodes to be missing during synchronization.

## Tree Structure

The node hierarchy has three types, all rooted in `SHAMapTreeNode`:

- **`SHAMapInnerNode`** — holds exactly `branchFactor = 16` logical child slots tracked by a 16-bit bitmask (`isBranch_`). Children are stored sparsely via `TaggedPointer hashesAndChildren_`, which manages the hash and child-pointer arrays together. A per-child atomic bitlock (`std::atomic<std::uint16_t> lock_`) allows concurrent descent without coarser locking.

- **`SHAMapLeafNode`** — wraps a `boost::intrusive_ptr<SHAMapItem const>` payload. Leaf nodes appear only at depth 64; the radix property guarantees their position already encodes the full key.

- **`SHAMapItem`** — the actual ledger object stored in a leaf. Its `uint256 tag_` is the key, and the variable-length body is stored immediately after the object header in the same allocation (a struct-hack layout). Allocations are served from a `SlabAllocatorSet` pre-configured for seven common size classes up to 1052 bytes, falling back to `new[]` only for oversized items.

## Copy-on-Write and Snapshots

The most important design choice in `SHAMap` is copy-on-write (CoW) sharing of tree nodes across map instances. Every `SHAMapTreeNode` carries a `cowid_` field: when non-zero it identifies the single `SHAMap` instance that has the right to mutate that node. A `cowid_` of zero means the node is shared and cannot be modified.

When `snapShot(isMutable)` is called, no tree nodes are copied. The snapshot and the original share the same physical nodes. The `cowid_` of the new map is set to the current `cowid_` of the original (for a mutable snapshot), and the original's `cowid_` is then incremented so it owns new nodes exclusively. Any subsequent write to either map that encounters a node owned by the other map calls `unshareNode()`, which clones the node and assigns it to the writer's `cowid_`. This makes snapshotting a closed ledger essentially free — the open ledger's `SHAMap` is snapshotted, the snapshot is made immutable, and the open ledger continues mutating under its own CoW identity.

## Memory Management

Two distinct intrusive pointer systems coexist. `boost::intrusive_ptr<SHAMapItem>` manages leaf payloads with simple reference counting. Tree nodes use `intr_ptr::SharedPtr<SHAMapTreeNode>` (aliased as `SharedIntrusive<T>`), a custom intrusive smart pointer that additionally supports weak references. The split matters: the node cache (`TreeNodeCache`) stores a `SharedWeakUnion` — a single pointer-sized value that holds either a strong or weak reference, toggled by a low-order tag bit — so that cached nodes can be promoted from weak to strong without a heap allocation. When a node's strong count drops to zero, `partialDestructor()` is called before the destructor proper; `SHAMapInnerNode` uses this hook to release its children early, allowing cascading collection without deep call stacks.

## State Lifecycle

`SHAMapState` is a four-state enum that controls what operations are legal:

- **`Modifying`** — open ledger; items can be added, removed, and updated.
- **`Immutable`** — closed ledger; all writes are forbidden. Asserts guard against attempts to mutate.
- **`Synching`** — the root hash is known (received from a peer) but interior nodes may be missing. `addRootNode()` and `addKnownNode()` feed incoming wire data into the map; `getMissingNodes()` probes for gaps.
- **`Invalid`** — synchronization failed or the map is known corrupt.

`setImmutable()` asserts that the current state is not `Invalid`, enforcing that only coherent maps are frozen.

## Synchronization Engine (`getMissingNodes`)

The peer-sync path is built around the private `MissingNodes` struct. It implements a depth-first traversal with bounded async I/O concurrency. A `std::stack<StackEntry, std::deque<StackEntry>>` drives the main DFS loop — the comment in the header explicitly calls out that `std::deque` is required here rather than `std::vector` because insertion and removal must not invalidate existing pointers or references. Nodes whose children are being fetched asynchronously are moved to a `resumes_` map, and a mutex+condition variable pair (`deferLock_`, `deferCondVar_`) coordinates the callback from `descendAsync()` back into `gmn_ProcessDeferredReads()`. The `generation_` field aligns with `SHAMapInnerNode::fullBelowGen_` — a monotonically increasing counter that marks subtrees already confirmed complete, allowing the traversal to skip entire branches on repeated calls.

## Merkle Proof API

`getProofPath(key)` returns the sequence of serialized nodes from the target leaf up to the root, with sibling hashes encoded in each parent's serialization. The companion static method `verifyProofPath(rootHash, key, path)` recomputes the root from the path without needing the full tree. These methods enable stateless light-client verification that a specific ledger object exists in a given ledger state.

## The `Family` Interface and Backing

`SHAMap` stores a reference to a `Family`, the abstract provider for the node store (`NodeStore::Database`), the `FullBelowCache`, the `TreeNodeCache`, and the "missing node" acquisition callbacks. The `Family` decouples storage policy from tree logic: two maps of the same ledger share a family and thus share their caches. Maps created with `setUnbacked()` (`backed_ = false`) skip nodestore writes entirely, useful for transient in-memory trees such as those built during transaction processing before they're committed.

## Iterator Design

`SHAMap::const_iterator` is a forward iterator over leaf nodes in key order. It carries its own `SharedPtrNodeStack` — a `std::stack` of `(SHAMapTreeNode, SHAMapNodeID)` pairs. This stack represents the path from the root to the current position, allowing `peekNextItem()` to resume descent without rescanning from the root. The iterator is always const because the tree's Merkle invariant requires that any write invalidate hashes all the way up to the root; a non-const iterator would either break the invariant silently or require expensive re-hashing on every dereference.

## Delta Computation

`compare(otherMap, differences, maxCount)` computes the symmetric difference between two maps, returning results as a `Delta` — a `std::map<uint256, DeltaItem>` where each `DeltaItem` is a `(before, after)` pair of `SHAMapItem` intrusive pointers. A null `before` means the item is new; a null `after` means it was deleted. This is used to determine exactly which account-state objects changed when a ledger closes, and which transactions are new relative to a peer's last-known ledger.