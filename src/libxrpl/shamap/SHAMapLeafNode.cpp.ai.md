# `SHAMapLeafNode.cpp` — Abstract Leaf Node Implementation

## Role in the SHAMap Hierarchy

The SHAMap data structure underlying the XRPL ledger is a Merkle Patricia trie. Its nodes split into two fundamental categories: inner nodes that branch the tree and leaf nodes that hold actual ledger data. `SHAMapLeafNode` occupies the middle tier of a three-level inheritance chain:

```
SHAMapTreeNode          (base: CoW identity, hash, refcounts)
  └── SHAMapLeafNode    (this file: item ownership, mutation, shared behavior)
        ├── SHAMapTxLeafNode
        ├── SHAMapTxPlusMetaLeafNode
        └── SHAMapAccountStateLeafNode
```

This file implements the behavior that is common to every leaf type — holding a `SHAMapItem`, providing read access, mutating with hash-change detection, debug formatting, and invariant checking — while leaving type-specific logic (hashing algorithm, wire serialization, node type identity) to the concrete subclasses.

## Copy-on-Write Integration

Both constructors are `protected`, not `public`. This is not an accident: `SHAMapLeafNode` is abstract in the design sense even though C++ does not mark it explicitly with a pure virtual function (those are pushed to the subclasses). Only the three concrete leaf classes can call these constructors from their own constructors.

The `cowid_` field, inherited from `SHAMapTreeNode`, encodes ownership in the CoW scheme. A nonzero `cowid_` means the node is exclusively owned by a particular `SHAMap` instance and carries unflushed changes. Zero means the node has been released into a shared state — it may simultaneously be referenced by multiple `SHAMap` views of the ledger. The `setItem()` method enforces this invariant directly:

```cpp
XRPL_ASSERT(cowid_, "xrpl::SHAMapLeafNode::setItem : nonzero cowid");
```

Mutating a shared (unowned) node would corrupt every `SHAMap` that holds a pointer to it. This assert is the runtime tripwire that catches any code path which forgets to first `clone()` the node before modifying it.

## Two Construction Paths

Two constructor overloads exist for two distinct situations. The first takes only an item and a `cowid`, leaving `hash_` default-initialized. Concrete subclasses using this overload immediately call `updateHash()` in their own constructor body to compute the hash fresh from the item. This is the path used when constructing a new node for the first time.

The second overload accepts a pre-computed `SHAMapHash` alongside the item and `cowid`. This is used during deserialization (`makeFromWire`, `makeFromPrefix`) and during `clone()` operations, where the hash is already known and recomputing it would be wasteful. Passing the hash directly into the base class sets `hash_` without triggering a SHA-512 half computation.

Both constructors assert `item_->size() >= 12`. The `size_` on a `SHAMapItem` reflects its data payload length, not the size of the object itself. The 12-byte floor is a protocol-level sanity guard: any serialized ledger object meaningful enough to appear in a SHAMap must carry at least this many bytes of content. This prevents degenerate or truncated objects from entering the tree.

## Item Mutation with Change Detection

`setItem()` replaces the held `SHAMapItem` and recomputes the hash via the virtual `updateHash()` dispatch. It returns a `bool` indicating whether the hash actually changed:

```cpp
auto const oldHash = hash_;
updateHash();
return (oldHash != hash_);
```

The return value is an optimization signal. Propagating a hash change upward through the tree — recomputing every ancestor inner node's hash — is expensive. If `setItem()` returns `false`, the caller knows the tree's Merkle root is unaffected and can skip that work entirely. In practice this situation is rare (replacing an item with one that produces the same hash would be unusual), but the check costs almost nothing and makes the SHAMap's mutation path more resilient to unnecessary work.

## `getString()` and Debug Formatting

`getString()` calls `SHAMapTreeNode::getString(id)` for position-level context and then appends type and content information. The type-dispatch calls `getType()`, which is pure virtual and resolved to the concrete subclass at runtime. This means `SHAMapLeafNode` produces a human-readable diagnostic string without knowing whether the item is a transaction, a transaction-with-metadata, or an account state object. The output includes the item's key (the 256-bit tag) and the item's data size in bytes alongside the node hash.

## Invariants and Finality

`invariants()` is marked `final override` in `SHAMapLeafNode`, sealing it against further override in concrete subclasses. It asserts two conditions that must hold for any valid leaf: the hash must be non-zero and the item pointer must not be null. These conditions collectively mean the leaf has been properly initialized and has not had its contents silently cleared. Similarly, `isLeaf()` and `isInner()` are both `final override` at this level, returning `true` and `false` respectively — no concrete leaf subclass can accidentally break the `isLeaf()` contract by overriding it again.

The `boost::intrusive_ptr` used for `item_` integrates with `SHAMapItem`'s custom `intrusive_ptr_add_ref` / `intrusive_ptr_release` hooks, which manage the object's lifetime through a slab allocator. This avoids the separate heap allocation that `std::shared_ptr`'s control block would require — relevant for an object type that may have millions of live instances during ledger processing.