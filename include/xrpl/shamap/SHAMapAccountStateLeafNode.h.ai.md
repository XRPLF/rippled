# `SHAMapAccountStateLeafNode`

## Role in the SHAMap

The XRPL's SHAMap is a Patricia trie whose leaves hold arbitrary typed blobs. Account state — the serialized form of ledger objects like account roots, offers, and trust lines — occupies one specific leaf variant: `SHAMapAccountStateLeafNode`. Alongside `SHAMapTxLeafNode` (transaction, no metadata) and `SHAMapTxPlusMetaLeafNode` (transaction with metadata), this class is one of three concrete leaf types that close the otherwise-abstract `SHAMapLeafNode` / `SHAMapTreeNode` hierarchy.

The separation into distinct concrete types rather than a single leaf class with a runtime flag is deliberate: each leaf type computes its Merkle hash using a different prefix and a different set of fields, and having the logic encoded statically via virtual dispatch eliminates branching in the hot path and makes the type distinction visible to the compiler.

## Inheritance and Memory Management

```
SHAMapTreeNode          (IntrusiveRefCounts, copy-on-write id, hash)
    └── SHAMapLeafNode  (holds item_, peekItem(), setItem())
            └── SHAMapAccountStateLeafNode  (final, this file)
```

The class also inherits `CountedObject<SHAMapAccountStateLeafNode>`, which provides lightweight global instance tracking. All tree nodes participate in intrusive reference counting via `IntrusiveRefCounts`, so they are managed through `boost::intrusive_ptr` and `intr_ptr::SharedPtr` rather than `std::shared_ptr`, avoiding the separate control-block allocation.

## Copy-on-Write Semantics and the Two Constructors

`SHAMapTreeNode` carries a `cowid_` field — a `uint32_t` that identifies which `SHAMap` instance owns this node. When `cowid_` is `0`, the node is unowned and shareable across multiple maps. When non-zero it belongs exclusively to one map and is considered dirty.

`SHAMapAccountStateLeafNode` exposes two constructors to serve this protocol:

1. **Construction with hash recomputation** (`item`, `cowid`): used when creating a brand-new node. It delegates to `SHAMapLeafNode` then immediately calls `updateHash()`.

2. **Construction with a pre-supplied hash** (`item`, `cowid`, `hash`): used by `clone()`. When the SHAMap needs to mutate a shared node it calls `clone()` to produce an exclusive copy; since the underlying item hasn't changed yet, recomputing the hash would be wasteful. Passing the known `hash_` directly skips the SHA computation.

`clone()` always uses the second form, forwarding the caller's new `cowid` and the current `hash_`:

```cpp
return intr_ptr::make_shared<SHAMapAccountStateLeafNode>(item_, cowid, hash_);
```

## Hash Computation — Why Account State Includes the Key

`updateHash()` feeds three pieces of data to `sha512Half`:

```cpp
hash_ = SHAMapHash{sha512Half(HashPrefix::leafNode, item_->slice(), item_->key())};
```

Contrast this with `SHAMapTxLeafNode::updateHash()`, which feeds only the `HashPrefix::transactionID` and `item_->slice()` — the key is absent. The reason is fundamental to how each leaf type is identified:

- A **transaction** is named by the hash of its own serialized content (i.e., the transaction ID *is* `sha512Half(prefix, blob)`). Including the key would double-count information already encoded in the blob.

- An **account state** object is keyed by an externally assigned ID (e.g., an account address or offer index) that does not necessarily appear verbatim in the serialized blob. Omitting the key from the hash would mean two objects with identical data but different keys would produce identical hashes, undermining the integrity of the state Merkle root. Including the key in the hash commitment binds the object firmly to its position in the trie.

`HashPrefix::leafNode` encodes the ASCII bytes `'M'`, `'L'`, `'N'` — a domain-separation prefix that prevents collisions between hash computations of different node types. The `serializeWithPrefix()` method replicates this exact sequence for external use (e.g., proof verification), emitting the same prefix, blob, and key in the same order.

## Wire Serialization

`serializeForWire()` is the peer-sync format: it writes the raw blob, then the key as a fixed-width bit string, then the single-byte wire type tag `wireTypeAccountState = 1`. The tag allows `SHAMapTreeNode::makeFromWire()` to reconstruct the correct concrete type on the receiving end. The `serializeWithPrefix()` method is used for hashing contexts and drops the wire tag (since the hash prefix already encodes the node type); it also uses `s.add32()` for the prefix rather than `s.add8()` so the domain prefix is exactly 4 bytes.

## Summary

`SHAMapAccountStateLeafNode` is a narrowly scoped, stateless policy class whose only responsibility is to encode the account-state-specific rules for hashing, cloning, type identification, and serialization. All mutable state lives in the base classes. Its `final` qualification, together with the two-constructor clone optimization and the key-inclusive hash formula, reflect careful attention to the performance and correctness requirements of the XRPL's cryptographic ledger state tree.