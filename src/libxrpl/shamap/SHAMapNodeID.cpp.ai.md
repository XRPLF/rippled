# SHAMapNodeID.cpp

## Role in the System

`SHAMapNodeID.cpp` implements the node-address type for the XRPL's `SHAMap` — a 16-ary Merkle patricia trie that underpins ledger state and transaction data. Every interior node and every leaf in the tree has an address consisting of two values: a `depth_` (how many levels below the root the node sits) and an `id_` (a `uint256` encoding the path taken to reach that node). This file provides all the logic for constructing, navigating, and serializing those addresses.

## The SHAMap Tree Shape

The tree has exactly 65 levels: a root at depth 0 and leaves at depth 64 (`SHAMap::leafDepth`). Each step down the tree consumes one 4-bit nibble of a 256-bit key, so 64 nibbles cover all 256 bits. Each inner node has exactly 16 possible children (`SHAMap::branchFactor = SHAMapInnerNode::branchFactor = 16`). This relationship between the tree geometry and the nibble-by-nibble key encoding is the load-bearing assumption of this entire file.

## The `depthMask()` Function

The pivotal private helper is `depthMask(unsigned int depth)`. It returns a `uint256` bitmask with exactly the high-order bits that are meaningful at that depth set to 1 and all lower bits set to 0. The mask table has 65 entries and is computed once at program startup inside a `static` local of a struct:

- At depth 0 (root), the mask is all-zero because the root carries no path prefix.
- At depth 1, the high nibble of byte 0 (`0xF0`) is set.
- At depth 2, the full first byte (`0xFF`) is set.
- Each subsequent pair of depths adds one nibble more.
- At depth 64 (leaf), all 32 bytes are fully set.

The loop advances two depths at a time, writing `0xF0` for odd depths and `0xFF` for even depths into the appropriate byte. This makes the masking exact: storing a node ID requires retaining only those bits that correspond to the path traversed so far, and zeroing everything else. Two nodes at the same depth on the same branch of the tree always have identical IDs regardless of which leaf key led to them — canonicality is enforced structurally.

## Constructor and Invariant Enforcement

The constructor `SHAMapNodeID(unsigned int depth, uint256 const& hash)` is strict: it asserts via `XRPL_ASSERT` both that `depth <= leafDepth` and that `hash == (hash & depthMask(depth))`. This means callers are expected to pass an already-masked value. The constructor does not silently mask for the caller; it treats a non-canonical hash as a programmer error. This is a deliberate safety choice — if the trie navigation produces a hash that doesn't conform, something upstream is broken, and catching it early is preferable to silently accepting corrupted state.

The static factory `createID(int depth, uint256 const& key)` is the one entry point that *does* mask on the caller's behalf, applying `key & depthMask(depth)` before delegating to the constructor. This is intended for building a node ID from a leaf's key: the caller knows the depth but supplies the full key, and `createID` strips the irrelevant bits. The asymmetry — constructor rejects unmasked input, factory method accepts it — makes the API's intent explicit.

## Descending the Tree: `getChildNodeID`

`getChildNodeID(unsigned int m)` produces the ID of child branch `m` (where `m` is 0–15). The design here is notable for its dual-layered error handling: an `XRPL_ASSERT` checks `depth_ <= leafDepth` (debug builds only), while a `Throw<std::logic_error>` also fires at runtime if `depth_ >= leafDepth`. The assert catches programmer misuse in debug mode, but the throw survives into release builds because asking for the children of a leaf is a genuine logic error that could arise from corrupted data, not just from mistakes during development.

A second throw guards against a corrupted `id_` that doesn't conform to `depthMask(depth_)`. This is a defensive check — in a well-formed system it should never trigger, but it provides a last-resort safeguard against a node that somehow reached an inconsistent state.

The actual bit manipulation is compact: after copying the parent's depth and ID, it OR-writes nibble `m` into the correct nibble position of `id_`:

```cpp
node.id_.begin()[depth_ / 2] |= ((depth_ & 1) != 0u) ? m : (m << 4);
```

When `depth_` is even the nibble to write occupies the high 4 bits of a byte (shift left 4), and when odd it occupies the low 4 bits. This mirrors how `depthMask` sets bits, keeping the two operations perfectly paired.

## `selectBranch`: Reading a Nibble

`selectBranch(SHAMapNodeID const& id, uint256 const& hash)` is the inverse of `getChildNodeID`: given a hash (a full leaf key), it extracts the nibble at the node's current depth to determine which of the 16 children to follow next. It reads the appropriate byte with `*(hash.begin() + (depth / 2))`, then shifts right by 4 or masks to 4 bits depending on whether the depth is even or odd. The result is always in [0, 15], confirmed by a final `XRPL_ASSERT`.

## Serialization and Deserialization

`getRawString()` serializes a node ID to 33 bytes: 32 bytes for the `uint256 id_` followed by 1 byte for `depth_`. This is the "wire format" documented in the header.

`deserializeSHAMapNodeID(void const* data, std::size_t size)` is the safe deserializer that returns `std::optional<SHAMapNodeID>` — returning `std::nullopt` on any validation failure rather than throwing. It checks exactly three things in order: the buffer must be 33 bytes, the depth byte must be within `[0, leafDepth]`, and the 256-bit prefix must satisfy the depth mask. Only when all three pass does it call the constructor. This three-layer check matches the constructor's own assertions but operates defensively because network-received data cannot be trusted.

## Design Tradeoffs

The static `masks_t` lookup table avoids recomputing masks on every tree traversal. Since the SHAMap is accessed on the hot path for every ledger state lookup, the single cache-friendly array read is preferable to recomputing masks via bit arithmetic. The `static const` inside the function guarantees thread-safe initialization in C++11 and later with no explicit synchronization needed.

The choice to make `SHAMapNodeID` copyable and comparable (full set of relational operators via `std::tie(depth_, id_)`) reflects its use as a map key and cache key throughout the XRPL node implementation — these operations need to be cheap and correct, which they are given that both fields are value types.