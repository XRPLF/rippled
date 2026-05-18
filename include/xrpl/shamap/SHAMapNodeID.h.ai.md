# `SHAMapNodeID` — Node Position Identifier in the SHAMap Radix-Merkle Tree

## Role in the System

`SHAMapNodeID` encodes the precise location of a node within a `SHAMap`, which is XRPL's hybrid data structure combining a 16-way radix tree with a Merkle tree. The SHAMap uses 256-bit keys where each nibble (4 bits) selects one of 16 branches at each successive level. A `SHAMapNodeID` answers the question: *which node in the tree are we talking about?* — expressed as a (depth, prefix) pair rather than a raw hash.

## The Two-Field Identity Model

Every `SHAMapNodeID` carries exactly two fields:

- `uint256 id_` — a 256-bit prefix, with only the top `depth_` nibbles populated; the rest are zero.
- `unsigned int depth_` — a level counter from 0 (root) to 64 (leaf level).

The tree has 65 levels because a 256-bit key is navigated nibble by nibble, and 256 / 4 = 64 branch levels sit below the root. The `branchFactor` of 16 and `leafDepth` of 64 are both defined as `constexpr` in `SHAMap` (which forwards `branchFactor` from `SHAMapInnerNode`).

The invariant that `id_` is always masked to `depth_` is enforced by the constructor via `XRPL_ASSERT`. This masking is what makes `id_` a *canonical path prefix* rather than an arbitrary 256-bit value — it uniquely and unambiguously identifies one node in the tree, not just any hash.

## The Depth Mask

The implementation-private `depthMask()` function (in `SHAMapNodeID.cpp`) builds a static table of 65 `uint256` bitmasks, one per depth. The mask for depth `d` has the top `d` nibbles set to all-ones and the remaining nibbles cleared. Concretely, the first byte holds `0xF0` at depth 1, `0xFF` at depth 2, the second byte adds `0xF0` at depth 3, `0xFF` at depth 4, and so on.

This pattern is why the constructor checks `id_ == (id_ & depthMask(depth))` — it verifies the id carries no data below its declared depth. Violating this would mean two logically distinct paths could produce the same `SHAMapNodeID`, breaking correctness guarantees.

## Child Navigation

`getChildNodeID(unsigned int m)` advances one level deeper by incrementing `depth_` and setting the nibble corresponding to that new depth to branch number `m` (0–15). The bit-manipulation is straightforward: since each byte holds two nibbles, even depths write the high nibble (`m << 4`) and odd depths write the low nibble (`m & 0xf`). An exception is thrown (not just asserted) when called on a leaf-depth node, because constructing a child `SHAMapNodeID` at depth 65 would violate the structural invariant.

The inverse operation, `selectBranch(SHAMapNodeID const& id, uint256 const& hash)`, is a free function: given a node's depth and a lookup key, it extracts the nibble at that depth from the key to determine which of the 16 branches to follow. This is the traversal primitive — every lookup, insert, or sync operation in `SHAMap` uses `selectBranch` to descend one level at a time.

## The `createID` Factory

`createID(int depth, uint256 const& key)` is the correct way to derive a `SHAMapNodeID` from a full leaf key when you need the ancestor at a specific depth. It applies `depthMask(depth)` to the key before constructing the object, discarding the lower nibbles automatically. This is essential during sync operations where you know the target key and the depth at which you want to reference an intermediate node.

## Wire Format and Deserialization

The on-wire representation is 33 bytes: 32 bytes of `id_` in big-endian uint256 format followed by a single byte for `depth_`. `getRawString()` produces this via the XRPL `Serializer`. The free function `deserializeSHAMapNodeID()` is the trusted deserialization entry point: it validates that the buffer is exactly 33 bytes, that the depth byte does not exceed 64, and critically, that the decoded `id_` satisfies the depth-mask invariant. Invalid input yields an empty `std::optional` rather than an exception, making it safe to use directly on untrusted peer data.

## Ordering and Comparisons

The full set of comparison operators is implemented manually (with a `FIXME-C++20` comment noting that the spaceship operator was not yet adopted). The primary comparator, `operator<`, sorts by `std::tie(depth_, id_)` — shallower nodes sort before deeper ones, and among same-depth nodes, ordering follows the 256-bit prefix value. This ordering is meaningful for `std::map` or `std::set` collections that track node frontiers during tree traversal or sync.

## Diagnostics Support

Inheriting from `CountedObject<SHAMapNodeID>` registers every live instance with a global lock-free counter. This is XRPL's lightweight diagnostic mechanism — a server can query `CountedObjects::getInstance().getCounts()` at runtime to see how many `SHAMapNodeID` objects are alive, useful for detecting leaks during sync operations.

The `to_string()` and `operator<<` overloads produce a human-readable `"NodeID(depth,hex_id)"` format (or `"NodeID(root)"` for the root), which is what appears in journal log messages throughout the tree traversal code.