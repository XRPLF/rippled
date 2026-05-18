# `SHAMapTxLeafNode` — SHAMap Leaf Node for Bare Transactions

## Role in the System

`SHAMapTxLeafNode` represents a leaf node in a `SHAMap` that stores a raw transaction **without** associated metadata. It is one of three concrete leaf types in the SHAMap node hierarchy, alongside `SHAMapTxPlusMetaLeafNode` (transaction + metadata) and `SHAMapAccountStateLeafNode` (ledger state). These three types cover every data element a SHAMap can hold, with the distinction between them being semantically significant: the transaction-only variant is used when building the transaction tree of an open or proposed ledger, before execution metadata is available. After a ledger closes and transactions are applied, the `SHAMapTxPlusMetaLeafNode` form is used instead.

## Class Hierarchy and Design

`SHAMapTxLeafNode` extends both `SHAMapLeafNode` and `CountedObject<SHAMapTxLeafNode>`. The `SHAMapLeafNode` base provides the shared `item_` field (an immutable, reference-counted `SHAMapItem`) and `SHAMapTreeNode` sits above that with the `hash_` and `cowid_` fields. The `CountedObject` mixin enables global tracking of live instances for memory diagnostics — a pattern shared across all three leaf node types and the inner node as well.

The class is declared `final`, which matches all three concrete leaf types. Since the entire polymorphism needed by `SHAMap` is captured in `SHAMapTreeNode`'s virtual interface, nothing needs to further subclass `SHAMapTxLeafNode`.

## Hashing: The Critical Distinction

The most architecturally significant aspect of this class versus its siblings is `updateHash()`. The hash is computed as:

```cpp
hash_ = SHAMapHash{sha512Half(HashPrefix::transactionID, item_->slice())};
```

Two details matter here. First, the hash prefix `HashPrefix::transactionID` (the 4-byte big-endian encoding of `'T','X','N'`) is prepended before the raw transaction bytes are fed to SHA-512-half. This prefix-before-hash approach is a deliberate domain-separation technique used throughout XRPL to ensure hashes of one type of object cannot collide with hashes of another type, even if the underlying byte content were identical. Second — and this is the key contrast with `SHAMapTxPlusMetaLeafNode` and `SHAMapAccountStateLeafNode` — the item's **key** is **not** included in the hash input. Those types pass both `item_->slice()` and `item_->key()` to `sha512Half`, but here only the raw blob is hashed. This reflects the fact that for a bare transaction, the transaction ID (key) is itself derived from the transaction's content, so it would be redundant; the hash fully characterises the content without it.

## Copy-on-Write Support

The two-constructor design is a direct consequence of the SHAMap's copy-on-write (CoW) semantics. When a `SHAMapTxLeafNode` is first built from raw item data, the single-`cowid` constructor is used, which calls `updateHash()` to compute the hash from scratch. The two-`cowid`+`hash` constructor is used by `clone()` — it accepts a pre-computed `hash_` from the original node and skips recomputation, which is correct because `clone()` only changes the ownership (`cowid`) while the underlying `item_` and its hash remain unchanged. Every node in the SHAMap hierarchy follows this same pattern.

## Serialization

Two serialization paths exist for every node type:

- `serializeForWire()` emits the raw item bytes followed by the constant `wireTypeTransaction` (`0`). This wire-type byte is the framing marker that `SHAMapTreeNode::makeFromWire()` uses to reconstruct the correct concrete node type on deserialization, routing to `makeTransaction()` in the factory.
- `serializeWithPrefix()` writes `HashPrefix::transactionID` followed by the raw item bytes, producing exactly the preimage used in `updateHash()`. This path supports rehashing from stored node data and canonical serialization for Merkle proof purposes.

Notably, neither path includes the item key. This contrasts with `SHAMapTxPlusMetaLeafNode::serializeForWire()`, which appends `item_->key()` followed by `wireTypeTransactionWithMeta` (`4`), reflecting that once metadata is attached the key carries additional information needed for reconstruction.

## Relationship to Sibling Types

All three leaf node types are structurally near-identical, differing only in hash prefix, wire type constant, and whether the key participates in hashing and wire serialization. The design deliberately pushes every type-specific behavior into these small final overrides rather than using runtime branching inside the base class. This keeps the base class clean, avoids `switch` statements on `SHAMapNodeType`, and allows the compiler to inline all the critical paths in `updateHash()` and `serialize*()` when working with concrete types. The `SHAMapNodeType::tnTRANSACTION_NM` enum value (`2`, "no metadata") serves as the identity marker for this type across the rest of the system, distinguishing it from `tnTRANSACTION_MD` (`3`) at the point where SHAMap tree contents are interpreted.