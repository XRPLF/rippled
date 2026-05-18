# `SHAMapTxPlusMetaLeafNode`

`SHAMapTxPlusMetaLeafNode` is one of three concrete leaf node types in the XRPL's SHAMap Merkle tree structure. It represents a transaction paired with its execution metadata — the canonical form of a transaction entry in a validated ledger's transaction map. Its counterpart, `SHAMapTxLeafNode`, holds transactions without metadata (used in open/proposed ledgers where execution has not yet occurred), while `SHAMapAccountStateLeafNode` covers ledger state entries.

## Role in the SHAMap Hierarchy

The class sits at the bottom of a three-level inheritance chain: `SHAMapTreeNode` → `SHAMapLeafNode` → `SHAMapTxPlusMetaLeafNode`. The base `SHAMapTreeNode` owns the `hash_` and `cowid_` fields and declares the copy-on-write interface; `SHAMapLeafNode` adds the `item_` pointer to the underlying `SHAMapItem` payload; this class provides the type-specific behavior for hashing and serialization.

The node is tagged `final`, so no further subclassing is permitted. It also inherits from `CountedObject<SHAMapTxPlusMetaLeafNode>`, which wires it into the global object telemetry system for tracking live instance counts — useful for diagnosing memory pressure in production.

## Two Constructors, One Design Reason

The class exposes two constructors intentionally. The single-hash-free constructor (taking only `item` and `cowid`) is used when building a new node from scratch: it calls `updateHash()` immediately after delegating to `SHAMapLeafNode`, so the node is always in a consistent hashed state. The second constructor (taking `item`, `cowid`, and an explicit `SHAMapHash`) is used when reconstituting a node from cached or network-received data where the hash is already known — it bypasses the recomputation and sets `hash_` directly via the base-class overload. This avoids the cost of redundant hashing during deserialization and is a common pattern across all three leaf node types.

## Hashing: Domain Separation is Critical

The `updateHash()` method is the most semantically significant part of the class:

```cpp
hash_ = SHAMapHash{sha512Half(HashPrefix::txNode, item_->slice(), item_->key())};
```

Three details matter here. First, the prefix `HashPrefix::txNode` encodes the bytes `'S'`, `'N'`, `'D'` — this is distinct from `HashPrefix::transactionID` (`'T'`, `'X'`, `'N'`) used by `SHAMapTxLeafNode` and `HashPrefix::leafNode` used by `SHAMapAccountStateLeafNode`. Hashing a prefix into every digest is the XRPL's standard defense against cross-context hash collisions: the same payload fed to different node types cannot produce the same hash, preventing forgery or confusion during Merkle proof verification.

Second, the key — the transaction's 256-bit identifier — is included in the hash input alongside the raw data slice. The metadata-free `SHAMapTxLeafNode` omits the key from its hash. This means the same transaction with and without metadata produces different hashes even under the same prefix, which is correct because the two node types occupy different tree contexts.

Third, this is marked `final override` even though the base class only requires `override`. This signals a deliberate design choice: the hashing algorithm for this node type is fixed and must not be overridden further.

## Serialization: Wire vs. Prefix Formats

Two serialization paths exist for fundamentally different purposes.

`serializeForWire` encodes the node for peer-to-peer transmission during SHAMap sync. It emits the raw payload slice, then the 32-byte key via `addBitString`, then the single-byte type identifier `wireTypeTransactionWithMeta` (value `4`). The type byte at the end allows the receiver's `SHAMapTreeNode::makeFromWire` to reconstruct the correct concrete type via the static factory. Compare this to `SHAMapTxLeafNode`, which omits the key from the wire format entirely and uses `wireTypeTransaction` (value `0`) — an important wire-protocol distinction.

`serializeWithPrefix` encodes the node for hashing, mirroring exactly what `updateHash()` computes. It prepends `HashPrefix::txNode` as a 32-bit big-endian value, then the slice and the key. This format is used to reconstruct and verify a hash from raw data without instantiating a full node — typically during Merkle proof validation. The agreement between `updateHash()` and `serializeWithPrefix` is an invariant the system relies on; any drift between them would silently corrupt hash verification.

## Copy-on-Write via `clone()`

```cpp
intr_ptr::SharedPtr<SHAMapTreeNode>
clone(std::uint32_t cowid) const override
{
    return intr_ptr::make_shared<SHAMapTxPlusMetaLeafNode>(item_, cowid, hash_);
}
```

This implements the copy-on-write protocol inherited from `SHAMapTreeNode`. When a SHAMap needs to modify a node it shares with another map instance (e.g., during ledger forking or state snapshots), it first calls `clone()` with its own `cowid`, producing a privately owned copy without recomputing the hash. The `item_` pointer is shared via `boost::intrusive_ptr<SHAMapItem const>` reference counting — the underlying payload is never deep-copied unless the item itself changes, keeping fork overhead minimal.

## Relationship to the Broader Transaction Model

In XRPL's ledger lifecycle, an open ledger's transaction map holds transactions as `tnTRANSACTION_NM` nodes (`SHAMapTxLeafNode`). Once a ledger closes and transactions are applied to the state, the resulting validated ledger's transaction map is rebuilt with `tnTRANSACTION_MD` nodes (`SHAMapTxPlusMetaLeafNode`), each pairing the original transaction with the `TxMeta` blob describing exactly what the transaction did. The node type distinction allows the same SHAMap infrastructure to serve both open and closed ledger contexts without ambiguity, with hash domain separation ensuring Merkle roots from the two contexts are structurally incompatible.