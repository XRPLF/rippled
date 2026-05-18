# `SHAMapTreeNode.h` — Base Class for SHAMap Tree Nodes

## Role in the System

`SHAMapTreeNode` is the polymorphic root of the XRPL ledger's Merkle radix-tree node hierarchy. Every node stored in a `SHAMap` — whether a branching interior node or a data-carrying leaf — derives from this class. The file also anchors the serialization protocol by defining the wire-format type tags and the `SHAMapNodeType` enumeration, making it the single authoritative location for the shape of the tree's on-disk and on-wire representations.

## Node Type System

The file establishes two parallel classification schemes for node types. The `SHAMapNodeType` enum (`tnINNER`, `tnTRANSACTION_NM`, `tnTRANSACTION_MD`, `tnACCOUNT_STATE`) is the in-memory identity used for runtime dispatch. The five `wireType*` constants are single-byte identifiers appended to the end of a serialized node payload during peer-to-peer sync. The comment "should not be arbitrarily changed" signals that these constants are part of the XRPL wire protocol and carry backward-compatibility obligations — a change here would break ledger sync across all node versions.

## Intrusive Reference Counting

`SHAMapTreeNode` inherits from `IntrusiveRefCounts`, which packs both strong and weak reference counts plus two lifecycle-state bits into a single 32-bit atomic integer. The design trades a degree of readability for tight storage: 16 bits of strong count, 14 bits of weak count, and 2 flag bits indicating whether the `partialDestructor` has started and finished. This matters because SHAMap nodes are extremely numerous (a full ledger tree can contain millions of nodes) and live across thread boundaries, so minimizing per-node overhead is a primary concern.

The `partialDestructor()` virtual method (overridden non-trivially only in `SHAMapInnerNode`) exists specifically to support weak intrusive pointers to inner nodes. When the last strong reference drops while weak references still exist, the partial destructor tears down the child pointers stored in the node — releasing child strong references — without freeing the memory, leaving the memory intact for weak pointer resolution until the last weak reference also drops.

## Copy-on-Write Semantics

The `cowid_` field (copy-on-write identifier) encodes which `SHAMap` instance "owns" a given node and is central to how the tree achieves both memory efficiency and mutation safety. A zero `cowid_` means the node is clean, unmodified, and eligible for sharing across multiple `SHAMap` instances simultaneously — for example, when taking a snapshot of the ledger state for parallel transaction processing.

When a `SHAMap` needs to mutate a node that it does not own (i.e., `cowid_` differs from the map's own ID), it calls `clone(cowid)` to produce a private copy marked with the new owner's ID. The original shared node is left intact and continues to be part of other maps. The `unshare()` method resets `cowid_` to zero, making a previously exclusive node available for sharing again — this is called when a node is flushed to the database, after which it becomes immutable.

Copy and assignment are deleted on `SHAMapTreeNode` itself, enforcing that duplication only ever happens through the controlled `clone()` factory, never by accident.

## Dual Serialization Contract

The class exposes two pure-virtual serialization methods that differ not in structure but in context and invariants:

- `serializeForWire(Serializer&)` — appends the single-byte `wireType*` tag at the end. This format is used during peer-to-peer sync. The type byte is at the end, not the beginning, which means the receiver must read the entire payload before identifying the node type.
- `serializeWithPrefix(Serializer&)` — prepends a 4-byte `HashPrefix` constant before the node data. This format is used for hashing (and for database storage). The prefix allows the receiver to identify the type from the first four bytes.

These two paths are matched by the two static factory constructors: `makeFromWire(Slice)` reads the last byte to dispatch to the appropriate private factory, while `makeFromPrefix(Slice, SHAMapHash)` reads the first four bytes as a `HashPrefix` enum and also accepts a pre-validated hash. The `hashValid` boolean propagated through the private `makeTransaction`, `makeAccountState`, and `makeTransactionWithMeta` helpers controls whether the known hash is passed directly to the concrete leaf node constructor or left to be computed later via `updateHash()`.

## Concrete Subclass Topology

`SHAMapTreeNode` is instantiated through three concrete leaf types — `SHAMapTxLeafNode` (bare transactions), `SHAMapTxPlusMetaLeafNode` (transactions with metadata), and `SHAMapAccountStateLeafNode` (account objects) — all of which pass through `SHAMapLeafNode` as an intermediate base that holds the `boost::intrusive_ptr<SHAMapItem const>` payload. `SHAMapInnerNode` is the branching node, holding a 16-way (`branchFactor = 16`) radix of child pointers stored in a compressed `TaggedPointer`-based representation.

The three private static factories are `private` to `SHAMapTreeNode` even though the instantiated types are defined externally because they encapsulate the decision logic for which concrete subclass to allocate — centrally in one file, keeping the public API clean and ensuring that only `makeFromWire` and `makeFromPrefix` are the valid entry points from outside the class.

## Invariant Checking

The pure-virtual `invariants(bool is_root)` method is a debug-mode consistency checker threaded through the entire class hierarchy. Each concrete node is responsible for asserting its structural preconditions, and the `is_root` parameter lets the root node relax the constraint that a node must have at least two children (a single-child inner node at the root is valid for a tree holding exactly one leaf). This pattern is a deliberate extension hook: new concrete node types are forced to implement `invariants()` by the compiler, ensuring correctness checks are never accidentally omitted.