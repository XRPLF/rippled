# `SHAMapLeafNode` — Abstract Base for SHAMap Leaf Nodes

## Role in the System

`SHAMapLeafNode` sits at the boundary between the structural layer of the SHAMap (tree topology and copy-on-write bookkeeping) and the semantic layer (what kind of ledger data a leaf actually holds). It is an intermediate abstract class — inheriting from `SHAMapTreeNode` and providing the shared mechanics of item storage, mutation, and identity queries — while deferring type-specific concerns like hash computation and serialization to three concrete subclasses: `SHAMapAccountStateLeafNode`, `SHAMapTxLeafNode`, and `SHAMapTxPlusMetaLeafNode`.

Every value in the SHAMap trie is ultimately a leaf node carrying a `SHAMapItem` — the 256-bit keyed, variable-length byte blob that represents either a ledger account state object, a bare transaction, or a transaction bundled with its execution metadata.

## Ownership and the Copy-on-Write Contract

The `cowid_` field, inherited from `SHAMapTreeNode`, is the pivot of the COW semantics. A non-zero value signals that this node is exclusively owned by a specific `SHAMap` instance and is therefore safe to mutate. A value of zero means the node is unowned — shareable across multiple map snapshots without copying.

`setItem()` enforces this invariant at the assertion level: `XRPL_ASSERT(cowid_, ...)` fires if you attempt to mutate a shared node. The caller is responsible for having cloned the node first (via the virtual `clone(cowid)` method defined in each concrete subclass), which produces a fresh owned copy with the new `cowid`. This design prevents accidental mutation of nodes that appear in historical or concurrent map views, which is critical for the ledger's snapshot and diff infrastructure.

## Item Lifecycle

`item_` is a `boost::intrusive_ptr<SHAMapItem const>` — deliberately `const`-qualified through the pointer. You can swap out which item a leaf points to (that is what `setItem()` does), but you can never modify the item itself in place. This immutability allows the same `SHAMapItem` to be referenced simultaneously from multiple nodes in different map versions without defensive copying.

Both constructors assert that `item_->size() >= 12`. This minimum-size guard reflects a hard protocol constraint: any well-formed XRPL serialized object is at least 12 bytes long. Accepting shorter data would indicate corruption or a programming error upstream.

The two-constructor split is intentional. The constructor that omits the hash is used for freshly created nodes; each concrete subclass calls `updateHash()` in its own constructor immediately afterward to compute and store the hash. The three-argument constructor that accepts a pre-computed `SHAMapHash` is used on deserialization paths where the hash is already known and need not be re-derived.

## Concrete Subclasses and Hash Divergence

The three concrete leaf types each implement `updateHash()` and `serializeForWire()` / `serializeWithPrefix()` differently, reflecting distinct wire-protocol formats:

- `SHAMapAccountStateLeafNode` hashes the item payload together with the item's 256-bit key under `HashPrefix::leafNode`. Both key and data are included because account state objects derive their canonical hash from their key.
- `SHAMapTxLeafNode` hashes only the raw item data under `HashPrefix::transactionID`, omitting the key. Transactions are identified by the hash of their content alone.
- `SHAMapTxPlusMetaLeafNode` hashes item data plus key under `HashPrefix::txNode`, since the combined transaction-plus-metadata blob requires the transaction ID as part of its canonical form.

This divergence is the reason `SHAMapLeafNode` cannot be made concrete: there is no single correct hash formula shared by all leaf types.

## Public Interface

`peekItem()` returns a const reference to the stored `intrusive_ptr`, giving callers access to the item's key and data without transferring ownership or bumping the reference count. The name "peek" is a deliberate convention in the XRPL codebase for zero-cost, non-owning access.

`setItem()` replaces the stored item, calls `updateHash()`, and returns `true` if the hash actually changed or `false` if the new item produces the same hash as the old one. This boolean return allows callers to skip unnecessary dirty-marking or re-indexing when the effective content is unchanged — a practical optimization in ledger state update paths.

`isLeaf()` and `isInner()` are sealed (`final override`) in `SHAMapLeafNode` itself, even though the class is not final. This is deliberate: the concrete subclasses are all leaf nodes, and overriding these methods further would be nonsensical. Sealing here prevents accidental polymorphic surprises in deep subclass chains.

`invariants()` asserts that the hash is non-zero and the item pointer is non-null. The `is_root` argument, meaningful for inner nodes (the root of a SHAMap is always an inner node), is silently ignored for leaves.

## Design Notes

Copy construction and copy assignment are deleted. The class relies entirely on `clone(cowid)` for duplication, which is more explicit about intent (creating an owned copy for mutation) than a general-purpose copy constructor would be. This prevents silent, expensive copies and keeps ownership semantics visible at the call site.

The `item_` field is `protected` rather than `private`, giving the concrete subclasses direct access for hashing and serialization without going through `peekItem()`. This trades encapsulation for the ability to write the concrete subclasses as headers-only (inline `updateHash()` and `serialize*()` implementations), which eliminates virtual call overhead in the hot hash-recomputation path.