# `SHAMapTreeNode.cpp` — Node Deserialization Factory

## Overview

`SHAMapTreeNode.cpp` provides the implementation of the static factory methods declared on the abstract `SHAMapTreeNode` base class. Its sole job is to reconstruct the correct concrete node object from raw serialized bytes — whether arriving over the peer-to-peer network wire, or being loaded from storage with an already-verified hash. The file contains no data members, no virtual dispatch, and no tree-walking logic; it is purely a deserialization layer that bridges raw bytes and the node type hierarchy.

## Node Type Hierarchy

`SHAMapTreeNode` is the abstract root of a four-class hierarchy. Leaf variants — `SHAMapTxLeafNode`, `SHAMapTxPlusMetaLeafNode`, and `SHAMapAccountStateLeafNode` — each hold a `SHAMapItem` (a ref-counted, slab-allocated object storing a 256-bit key and an opaque payload). `SHAMapInnerNode` is the branching node with up to sixteen child slots. The factory methods in this file instantiate all four concrete types but declare `makeTransaction`, `makeAccountState`, and `makeTransactionWithMeta` as `private` on the header, keeping them as internal routing helpers called only from the two public entry points.

## Two Serialization Formats

The two public entry points correspond to two different on-wire representations:

**`makeFromWire(Slice rawNode)`** handles the legacy wire format. The type discriminant is a single byte appended to the *end* of the buffer (`rawNode[rawNode.size() - 1]`), and the five possible values are the `wireType*` constants defined in `SHAMapTreeNode.h` (e.g., `wireTypeTransaction = 0`, `wireTypeAccountState = 1`). The slice is trimmed of that trailing byte with `remove_suffix(1)` before being passed downstream. Because the wire format carries no pre-computed hash, `makeFromWire` always passes `hashValid = false`, letting each concrete node's constructor call `updateHash()` to compute the hash from scratch.

**`makeFromPrefix(Slice rawNode, SHAMapHash const& hash)`** handles the prefixed format used for hash verification. Here the first four bytes encode a `HashPrefix` enum value (the canonical domain-separation prefix used throughout the XRP Ledger's hashing scheme), extracted with explicit big-endian byte arithmetic and then stripped via `remove_prefix(4)`. Because the hash is supplied externally (already verified by the caller), `hashValid = true` is passed through, bypassing recomputation in the leaf constructors. This is the path taken when retrieving nodes from a trusted node store or receiving them from a sync partner that already attested to their hash.

The `hashValid` flag flows all the way into the leaf constructors via overloaded forms: when `false`, the single-argument constructor calls `updateHash()` immediately; when `true`, the two-argument constructor takes the hash directly into `hash_` and skips recomputation.

## Tag Extraction

The three leaf factories differ in how they derive the `SHAMapItem` key (`tag`):

- **`makeTransaction`** computes the key on-the-fly: `sha512Half(HashPrefix::transactionID, data)`. The raw transaction bytes carry no embedded key — the key *is* the hash of the content. This is consistent with `SHAMapTxLeafNode::updateHash()`, which uses the same formula.

- **`makeTransactionWithMeta`** and **`makeAccountState`** extract a 32-byte `uint256` tag that is appended to the *tail* of the payload during serialization (see `serializeForWire()` in both leaf node headers, which calls `s.addBitString(item_->key())`). Both helpers use `s.getBitString(tag, s.size() - tag.bytes)` to read those last 32 bytes, then `s.chop(tag.bytes)` to remove them before creating the item. This separation is necessary because `SHAMapItem` stores the key and the payload separately, so the key must be peeled off before the `make_shamapitem` call.

The `isZero()` guard in `makeAccountState` is a business-logic invariant: a zero `uint256` is not a valid ledger object identity and would represent a corrupted or malicious node. No equivalent guard exists for transaction nodes because the transaction key is computed, not loaded.

## Validation and Error Handling

Each path validates its input at the earliest possible point:

- `makeFromWire` returns a null `intr_ptr::SharedPtr<SHAMapTreeNode>{}` on empty input rather than throwing — the caller is expected to treat null as "no node". Unknown type bytes throw `std::runtime_error`.
- `makeFromPrefix` requires at least 4 bytes for the prefix; shorter input throws immediately.
- Both `makeTransactionWithMeta` and `makeAccountState` perform an explicit size check before calling `getBitString`, throwing `std::runtime_error("Short TXN+MD node")` or `std::runtime_error("short AS node")` respectively. The subsequent `getBitString` call provides a second layer of validation (returning false on failure, which throws `std::out_of_range`). Both layers are noted with `// FIXME: improve this interface` — the manual pre-check exists because the `getBitString` API does not distinguish between a size-zero input and a successful zero read.

## Copy-on-Write Semantics

Every factory method constructs nodes with `cowid = 0`. In the SHAMap copy-on-write design, `cowid_ == 0` signals that a node is not dirty and not exclusively owned by any map, making it immediately eligible for sharing across multiple `SHAMap` instances. Newly deserialized nodes are inherently read-only — they haven't been modified by any map — so starting them as unowned is the correct default. A node becomes owned (and non-shareable) only when a map needs to mutate it, at which point it is cloned with a non-zero `cowid`.

## `getString`

The `getString(SHAMapNodeID const&)` override is a trivial delegation to `to_string(id)`, providing a human-readable positional description for debugging and logging. It is the only non-factory method implemented in this file and has no architectural significance.