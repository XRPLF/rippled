# SHAMap

Authenticated 16-way radix Merkle trie. Every ledger has two: a TRANSACTION tree (txid → tx, with or without metadata) and a STATE tree (object key → serialized object). Root hash is what validators sign — two nodes agree on ledger state iff their root hashes match. Tree depth is fixed at 64 (256-bit keys consumed 4 bits per level, 65 levels total: root at depth 0, leaves at depth 64).

Root is always a `SHAMapInnerNode`. Leaves only appear at depth 64.

## Core Invariants

- **Tree shape**: `branchFactor = 16`, `leafDepth = 64`, max 65 levels (root at depth 0). One nibble per level.
- **CoW ownership**: each node has a `cowid_`. Non-zero → exclusively owned by that map and mutable. Zero → shared/canonicalized, must not be mutated. `setItem()`, `setChild()`, `dirtyUp()` all assert `cowid_ != 0`.
- **`unshareNode` before any mutation** of a shared node — #1 bug class. Skipping it corrupts every snapshot sharing the node.
- **`canonicalize`** ensures one in-memory instance per hash via `Family::getTreeNodeCache()`. Asserts `cowid == 0` on entry AND on values returned by `cacheLookup()`.
- **`SHAMapNodeID` masking**: `id_ == (id_ & depthMask(depth_))` is enforced by constructor. Two nodes at the same depth on the same path always have identical `SHAMapNodeID`.
- **Inner node concurrency**: per-child bit spinlock in `lock_` (`std::atomic<uint16_t>`, one bit per branch). Allows concurrent reads of different children. `setChild`/`shareChild` skip locking because they require CoW ownership.
- **Leaf size floor**: `SHAMapItem::size() >= 12` asserted at leaf construction.

## State Machine

```cpp
enum class SHAMapState {
    Modifying = 0,  // open ledger — can add/remove/update
    Immutable = 1,  // frozen — no writes; asserts guard
    Synching  = 2,  // root hash known; missing nodes can be added
    Invalid   = 3,  // corrupt; consensus must discard
};
```

`setImmutable()` asserts state ≠ `Invalid`. Hash-mismatch or position-mismatch during `addKnownNode` transitions to `Invalid` (not a crash).

## Copy-on-Write Discipline (#1 Bug Class)

```cpp
// REQUIRED before mutating any shared node:
auto node = unshareNode(branch, key);  // clones if shared
node->setChild(index, child);          // safe to modify
```

`snapShot(isMutable)` does NOT copy nodes. It bumps the original's `cowid_` and shares the `root_` pointer. Subsequent writes call `unshareNode()` which clones on first touch. Immutable snapshots of immutable maps share everything with zero clone cost.

The copy constructor increments `cowid_` (`cowid_(other.cowid_ + 1)`) and calls `unshare()` if either side is mutable, preventing later mutations from racing. An immutable copy of an immutable map shares all nodes with zero clone cost.

`getHash()` does `const_cast<SHAMap&>(*this).unshare()` when the root hash is zero — acknowledged design compromise (logical read, physical mutate).

## Key Files

- `include/xrpl/shamap/SHAMap.h` — main class, state machine, `MissingNodes` struct
- `include/xrpl/shamap/SHAMapTreeNode.h` — base; `cowid_`, `hash_`, `IntrusiveRefCounts`, wire-type constants
- `include/xrpl/shamap/SHAMapInnerNode.h` — 16-way branch, sparse `TaggedPointer`, per-bit spinlocks, `fullBelowGen_`
- `include/xrpl/shamap/SHAMapLeafNode.h` — abstract leaf base, `item_` slot, `setItem` returns hash-changed bool
- `include/xrpl/shamap/SHAMapTxLeafNode.h` — tx without metadata (`tnTRANSACTION_NM`)
- `include/xrpl/shamap/SHAMapTxPlusMetaLeafNode.h` — tx with metadata (`tnTRANSACTION_MD`)
- `include/xrpl/shamap/SHAMapAccountStateLeafNode.h` — account state leaf (`tnACCOUNT_STATE`)
- `include/xrpl/shamap/SHAMapItem.h` — slab-allocated, struct-hack payload, intrusive refcount
- `include/xrpl/shamap/SHAMapNodeID.h` — (depth, masked-prefix) tree address
- `include/xrpl/shamap/SHAMapMissingNode.h` — exception + `SHAMapType` enum (TX/STATE/FREE)
- `include/xrpl/shamap/SHAMapAddNode.h` — useful/duplicate/invalid accumulator for sync results
- `include/xrpl/shamap/SHAMapSyncFilter.h` — pull/notify interface for fetch packs and ephemeral caches
- `include/xrpl/shamap/Family.h` — bundles NodeStore, two caches, missing-node recovery
- `include/xrpl/shamap/FullBelowCache.h` — "subtree complete locally" memo with generation counter
- `include/xrpl/shamap/TreeNodeCache.h` — `TaggedCache<uint256, SHAMapTreeNode>` with intrusive ptrs
- `include/xrpl/shamap/detail/TaggedPointer.h` / `.ipp` — sparse 16-child storage with 2-bit tag
- `src/libxrpl/shamap/SHAMap.cpp` — mutation (add/del/update), traversal, fetch, flush
- `src/libxrpl/shamap/SHAMapSync.cpp` — getMissingNodes, getNodeFat, addRootNode/addKnownNode, proofs
- `src/libxrpl/shamap/SHAMapDelta.cpp` — compare, walkMap, walkMapParallel
- `src/libxrpl/shamap/SHAMapInnerNode.cpp` — sparse storage mechanics, hash, serialization
- `src/libxrpl/shamap/SHAMapLeafNode.cpp` — abstract leaf shared behavior
- `src/libxrpl/shamap/SHAMapTreeNode.cpp` — deserialization factories
- `src/libxrpl/shamap/SHAMapNodeID.cpp` — depth mask table, branch nav, wire format

## Three Concrete Leaf Types

All inherit `SHAMapLeafNode` (which inherits `SHAMapTreeNode`). All `final`. Differ only in hash prefix, wire-type byte, and whether the key is fed into the hash.

| Class | Hash prefix (bytes) | Key in hash? | Key in wire? | Wire-type byte |
|---|---|---|---|---|
| `SHAMapTxLeafNode` | `HashPrefix::transactionID` (`'T','X','N'`) | no | no | `wireTypeTransaction = 0` |
| `SHAMapTxPlusMetaLeafNode` | `HashPrefix::txNode` (`'S','N','D'`) | yes | yes | `wireTypeTransactionWithMeta = 4` |
| `SHAMapAccountStateLeafNode` | `HashPrefix::leafNode` (`'M','L','N'`) | yes | yes | `wireTypeAccountState = 1` |

**Why the asymmetry**: a transaction's ID *is* `sha512Half(prefix, blob)`, so the key is already implied by the data. Account state and tx+meta keys (account address, offer index, etc.) do NOT appear in the blob and must be hashed in — otherwise two distinct objects with identical payloads would collide.

Open ledgers hold transactions as `tnTRANSACTION_NM`; closed ledgers rebuild the tx tree as `tnTRANSACTION_MD` after metadata is attached. Hash prefix difference makes the two roots structurally incompatible — by design.

Each concrete leaf has two constructors: hash-recompute (used by initial `make_*`) and hash-supplied (used by `clone()` and deserialization with `hashValid = true`).

**`SHAMapLeafNode`** is the intermediate abstract base. Both constructors are `protected` — only concrete subclasses call them. `isLeaf()`, `isInner()`, and `invariants()` are sealed `final override` here. `invariants()` asserts hash non-zero and item non-null. `item_` is `protected` (not `private`) so concrete subclasses can access it directly in their inline `updateHash()` implementations, avoiding virtual dispatch overhead in the hash path.

## SHAMapItem

Single allocation: struct fields + payload bytes via struct hack (placement-new'd after `sizeof(*this)`). Constructor is `private`, all copy/move deleted; only path is `make_shamapitem()`.

Backed by `detail::slabber` — a `SlabAllocatorSet<SHAMapItem>` with seven tiers (128/192/272/384/564/772/1052 extra bytes, 40–60 MiB pools each, 2 MiB block alignment for THP). Falls back to `new uint8_t[]` for oversize. Max payload 16 MiB (asserted).

`refcount_` is `mutable std::atomic<uint32_t>`. `intrusive_ptr_add_ref` calls `LogicError` if count was already zero (resurrection guard). `intrusive_ptr_release` runs `std::destroy_at` then returns memory to `slabber.deallocate()`, falling through to `delete[]` when the pointer didn't come from a slab.

`SHAMapLeafNode::item_` is `boost::intrusive_ptr<SHAMapItem const>` — items are immutable; mutation produces a new item. The `const` through the pointer allows the same `SHAMapItem` to be shared across CoW snapshots without copying.

## Inner Node: Sparse Storage via TaggedPointer

`SHAMapInnerNode::hashesAndChildren_` is a single `TaggedPointer` holding two co-located arrays (`SHAMapHash[N]` followed by `SharedPtr<SHAMapTreeNode>[N]`). N comes from `boundaries = {2, 4, 6, 16}` indexed by the 2-bit tag stored in the pointer's low bits.

- `SHAMapHash` is `static_assert`'d to have `alignof >= 4`, freeing the low 2 bits.
- Tag 3 (N=16) is the **dense** case; tags 0–2 are sparse, with non-empty children packed in branch-number order.
- `isBranch_` (`uint16_t`) is the authoritative occupancy bitset. Translation: `getChildIndex(i) = popcnt16(isBranch_ & ((1<<i) - 1))`.
- Four `boost::singleton_pool`s (one per size class, 512 KiB blocks) back allocations. Dispatch is via `std::array<std::function<...>, 4>` indexed by tag — O(1), no virtual calls.

This typically cuts inner-node memory to ~25% of a dense layout.

**Resize**: `resizeChildArrays()` uses `TaggedPointer`'s move-restructuring constructors. The 4-arg form (`srcBranches`, `dstBranches`, `toAllocate`) handles simultaneous reshape — in-place if size class is unchanged (shifts within the existing allocation), otherwise allocates new and placement-copies.

**`RawAllocateTag` constructor**: allocates without running element constructors. Used internally only, always paired with explicit placement-new loops; destructor unconditionally runs explicit destructors.

`iterChildren(F)` exposes all 16 logical branches (zero-hash for empties — needed for `updateHash`). `iterNonEmptyChildIndexes(F)` gives `(branchNum, arrayIdx)` — used for mutation and serialization.

**Two compile-time constraints** in `TaggedPointer.ipp`: `boundaries.size() <= 4` (tag is exactly 2 bits) and `boundaries.back() == branchFactor`. Adding a 5th boundary fails the build.

## SHAMapNodeID

Two fields: `uint256 id_` (path prefix, masked to depth) and `unsigned depth_` (0–64). The static `depthMask()` table has 65 entries: nibble `d/2` gets `0xF0` at odd depths, `0xFF` at even, accumulating top-down.

- Constructor **rejects** unmasked input (asserts `id == id & depthMask`).
- `createID(depth, key)` is the factory that **applies** the mask for you. Asymmetric API by design.
- `getChildNodeID(m)` throws (not just asserts) at `leafDepth` — corrupted data may trigger this in release builds.
- `selectBranch(id, hash)` reads the nibble at `id.depth` from `hash`. The traversal primitive used everywhere.
- Wire format: 33 bytes (32 id + 1 depth). `deserializeSHAMapNodeID` returns `std::optional` and validates size, depth ≤ 64, and the mask invariant.

`operator<` sorts by `std::tie(depth_, id_)` — shallower before deeper, then by prefix value. Used as map/set key for traversal frontiers.

## Hash Computation

Inner: `sha512Half(HashPrefix::innerNode, hashes[0..15])` — always feed all 16, zeros for empties. Hash is identical regardless of dense/sparse storage.

`updateHash()` reads from `hashes` array directly. `updateHashDeep()` pulls hashes from child pointers first, then computes — used after batch mutations where in-memory child hashes were updated but the local hashes array wasn't synced.

## Wire Serialization Formats

**Inner nodes** — two formats chosen by occupancy in `serializeForWire`:
- **Compressed** (< 12 children): per non-empty branch, 32-byte hash + 1-byte branch index, total 33·n bytes.
- **Full** (≥ 12 children): all 16 hashes in order, 512 bytes.

Type byte appended at the end. `makeFullInner()` / `makeCompressedInner()` are the matching factories, each validating exact size (throws `std::runtime_error` on mismatch — not silent corruption).

`serializeWithPrefix` always emits the full 16-hash form prefixed with `HashPrefix::innerNode` (used for hashing).

**Leaves** — wire format trails with the single-byte wire-type tag at the END (not start). `makeFromWire` reads `rawNode[size-1]` to dispatch.

`makeFromPrefix(slice, hash)` uses the leading 4-byte `HashPrefix` and is the trusted (hash-known) path — propagates `hashValid = true` to skip recompute in leaf constructors.

**Tag extraction asymmetry**: `makeTransaction` recomputes the key as `sha512Half(HashPrefix::transactionID, data)`. `makeTransactionWithMeta` and `makeAccountState` read the 32-byte key from the *tail* of the payload (via `getBitString`), then `chop` it before creating the `SHAMapItem`. A manual pre-check guards against zero-size reads before calling `getBitString` — see `SHAMapTreeNode.cpp` comment `// FIXME: improve this interface`.

## Mutation Mechanics

All three mutations follow: walk-with-stack → local change → `dirtyUp()`.

- **`addGiveItem`**: empty branch → create leaf there. Collision with existing leaf → loop creating inner nodes deeper until keys diverge (respects merge property — inner nodes only where ≥2 items coexist below).
- **`delItem`**: drop the leaf, walk up reducing child counts. Zero children → null out. One child + `onlyBelow()` confirms a single leaf below → collapse inner, hoist leaf upward via `makeTypedLeaf` with the original leaf type.
- **`updateGiveItem`**: locate, unshare, swap payload; call `dirtyUp` only if `setItem()` returns `true` (hash actually changed). Avoids spurious rehashing.
- **`dirtyUp`**: consume stack bottom-up, `unshareNode` each, `setChild` to link in the updated subtree. Produces a CoW-owned chain from mutation point to root.

`makeTypedLeaf()` maps `SHAMapNodeType` to one of three concrete leaf classes. Unrecognized types throw `LogicError` immediately.

## Node Fetching (Backed vs Unbacked)

`backed_ = true` integrates with `Family::db()`; `backed_ = false` (set via `setUnbacked()`) is in-memory only (e.g., transient tx-processing trees).

`fetchNodeNT` tiered lookup:
1. `cacheLookup()` → `Family::getTreeNodeCache()`
2. `fetchNodeFromDB()` → `Family::db().fetchNodeObject()`
3. `checkFilter()` → `SHAMapSyncFilter::getNode()`, then notifies via `gotNode(true, ...)`

Misses return `nullptr` (`fetchNodeNT`) or throw `SHAMapMissingNode` (`fetchNode`, `descendThrow`). `finishFetch` catches `std::runtime_error` from deserialization, logs, and suppresses (doesn't crash).

On miss, `full_` is cleared and `Family::missingNodeAcquireBySeq(seq, hash)` is called — links to inbound-ledger acquisition pipeline.

`descendAsync` is the non-blocking variant: posts `Family::db().asyncFetch()`, sets `pending = true`, invokes user callback on completion.

## Missing-Node Discovery (`getMissingNodes`)

`MissingNodes` inner struct holds traversal state. Several details matter:

- **`stack_` is `std::stack<StackEntry, std::deque<StackEntry>>`** — NOT vector. Raw `SHAMapInnerNode*` pointers held in entries must remain valid across pushes; vector reallocation would invalidate them.
- **Random start nibble**: `firstChild = rand_int(255)` per stack entry. Concurrent callers on the same map produce different request sets — maximizes coverage when many peers ask in parallel.
- **`maxDefer_ = 512`** in-flight async reads. When reached, `gmn_ProcessDeferredReads` blocks on a CV draining the batch. Completed nodes are canonicalized and the parent is revisited via `resumes_`.
- **FullBelow short-circuit**: before descending, `touch_if_exists(hash)` on the cache; on success, skip the subtree. After confirming complete, `insert(hash)` and `setFullBelowGen(generation)` on the in-memory node.
- Two helpers: `gmn_ProcessNodes` (per-node descent + bookkeeping), `gmn_ProcessDeferredReads` (I/O completion handler).

**Gotcha**: processing async completions out of order would mark "full below" incorrectly. The `resumes_` map + deferred-batch barrier prevent this.

## Serving Nodes to Peers: `getNodeFat`

Bundles a target node plus a bounded-depth subtree in one response to amortize sync latency. Depth budget only decrements when an inner node has > 1 child — single-child chains (compressed radix paths) traverse for free. `fatLeaves=true` includes adjacent leaves; otherwise inner-only.

`visitNodes` (used by `visitDifferences` and traversal helpers) uses an explicit `std::stack` to avoid recursion on potentially 64-level trees.

`visitDifferences` short-circuits at hash equality — O(1) when subtrees match. The `have` pointer is nullable: null means "report everything in `this`."

## Ingesting Nodes: `addRootNode` / `addKnownNode`

Returns `SHAMapAddNode` (tri-state: useful / duplicate / invalid). Aggregated via `+=` across batches in `InboundLedger`.

`addKnownNode` performs **two integrity checks**:
1. Deserialized node's hash matches the parent-branch hash.
2. For leaves at `leafDepth`: reconstructed `SHAMapNodeID` from the leaf's actual key matches the claimed `SHAMapNodeID`. This closes a theoretical hash-collision-at-wrong-position attack.

Mismatch transitions the map to `Invalid` — graceful, not a crash. Skips descent into FullBelow subtrees.

**Gotcha**: callers must handle `invalid` gracefully — empty branch or hash mismatch on traversal is legitimate when peer data is stale.

`isGood()` returns `(good + duplicate) > bad` — duplicates count positively (benign), only `bad` is evidence of misbehavior. `isUseful()` is stricter: did we actually make progress?

## Merkle Proofs

`getProofPath(key)` collects nodes from leaf to root (via `walkTowardsKey` with stack), serialized leaf-first.

`verifyProofPath(rootHash, key, path)` is **static** — no live tree needed. Walks root-to-leaf, verifying each hash and using `selectBranch` to pick the next expected hash. Length bounded at 65. Deserialization wrapped in try/catch (network input).

**Gotcha**: wrong key at any level causes false negative — verifier walks down using the *supplied* key's nibbles, not anything inside the path data.

## Comparison / Delta (`SHAMapDelta.cpp`)

`compare` short-circuits at root: `if (getHash() == other.getHash()) return true`. O(d) in the number of differences, not O(n) in total items.

Returns `Delta = std::map<uint256, DeltaItem>` where `DeltaItem = pair<SHAMapItem ptr, SHAMapItem ptr>`. Null first → added; null second → deleted; both → modified.

Four dispatch cases at each pair (leaf/leaf, inner/leaf, leaf/inner, inner/inner). Asymmetric cases delegate to `walkBranch`, which uses an `isFirstMap` bool to preserve (first-map version, second-map version) ordering in the pair regardless of which side is the subtree.

**`maxCount` defense**: passed by reference into `walkBranch`, decremented per insertion. Returns false on truncation. The ledger-diff RPC passes `INT_MAX` (unlimited); the sync RPC passes 256 (bounded exposure to malicious or fabricated diffs).

## walkMap / walkMapParallel

Completeness check: traverses, recording any node hash referenced but absent (via `descendNoStore`, which returns null instead of throwing) into a `vector<SHAMapMissingNode>`.

**`walkMapParallel`** partitions at depth 1 — one `std::thread` per non-empty, non-leaf top-level child (up to 16-way). Each thread has its own `nodeStack`; `missingNodes` and an `exceptions` vector are mutex-shared.

**Critical**: an uncaught exception in a `std::thread` calls `std::terminate`. Worker lambda catches `SHAMapMissingNode` and records to `exceptions` instead — must inspect on return. Return value `true` ⇔ no thrown exceptions, NOT ⇔ no missing nodes (those are always in the vector).

## Family Interface

`Family` is the abstract collaborator bundle: `db()`, `getFullBelowCache()`, `getTreeNodeCache()`, `missingNodeAcquireBy{Seq,Hash}()`, `journal()`, `sweep()`, `reset()`.

Non-copyable, non-movable (SHAMap stores `Family&`; moving would dangle references).

Production impl: `NodeFamily` (in `src/xrpld/shamap/`). Tests use lighter-weight in-memory versions in `src/test/shamap/common.h`.

`NodeFamily::missingNodeAcquireBySeq` maintains a `maxSeq_` high-water under `maxSeqMutex_` to avoid redundant acquisition requests when many SHAMaps simultaneously hit missing nodes.

## FullBelowCache

`KeyCache<uint256>` — stores only hashes (no values), thread-safe, time-expiring (default 2 minutes). Wrapped in `BasicFullBelowCache` to add a generation counter.

**Two-layer invalidation**:
- Per-node `fullBelowGen_` on `SHAMapInnerNode` (compared against current cache generation in `isFullBelow(gen)`).
- The cache itself (queried via `touch_if_exists`).

`clear()` purges entries AND increments `m_gen` — zero-cost global invalidation of every in-memory marker (mismatched generation → `isFullBelow` returns false). `reset()` purges and sets `m_gen = 1` (used at startup / hard reset). The distinction matters: any node carrying `fullBelowGen_ > 1` won't match the reset-to-1 state — correct, because those nodes are expected to be recreated fresh.

Only `backed_ = true` maps participate.

## TreeNodeCache

```cpp
using TreeNodeCache = TaggedCache<
    uint256, SHAMapTreeNode, false,
    intr_ptr::SharedWeakUnionPtr<SHAMapTreeNode>,
    intr_ptr::SharedPtr<SHAMapTreeNode>>;
```

Two reasons for intrusive (not `std::shared_ptr`):
1. **Earlier memory reclamation**: `SHAMapInnerNode::partialDestructor()` releases the 16-way child array as soon as the strong count hits zero, even while weak references in the cache are still live. `std::make_shared` co-allocates control block + object, blocking reclamation until all weaks expire.
2. **Single-word strong/weak**: `SharedWeakUnion<T>` uses one pointer-sized word with a low-bit tag (alignment guarantees the bit is free). Demoting hot → cold flips one bit in place instead of swapping `shared_ptr` ↔ `weak_ptr`.

## Canonicalization

`canonicalize(hash, nodePtr)` deduplicates: if the cache already holds this hash, replace the local pointer with the cached one; otherwise insert. Asserts `cowid == 0` (only shareable nodes can be canonical). `cacheLookup()` asserts the same on returned nodes.

`canonicalizeChild()` on inner nodes: when two threads concurrently fetch the same child from disk, the per-child spinlock serializes; first writer wins, late writer's freshly-deserialized node is discarded. The incumbent hash is verified to match before installation.

## SyncFilter

Two-method interface bridging SHAMap to peer fetch packs and consensus tx caches.

- `getNode(hash) -> optional<Blob>`: filter's chance to supply a node from a transient source (fetch pack, consensus cache).
- `gotNode(fromFilter, hash, ledgerSeq, Blob&&, type)`: notification of node receipt. **`Blob&&` may be moved/destroyed — do not reuse**. `fromFilter=true` means data came from this filter's own `getNode` (no need to re-store); `false` means it came from the network and should be persisted.

Implementations:
- `AccountStateSF`, `TransactionStateSF` — write to NodeStore + fetch pack. Only used on add paths.
- `ConsensusTransSetSF` — backed by `TaggedCache<SHAMapHash, Blob>`. Used on both add AND check (since the backing store is purely transient).

## Flushing

`walkSubTree(doWrite, type)` is post-order DFS with **explicit stack** (tree may be 64 deep — recursion risks stack overflow). Per node: `preFlushNode()` clones if needed (protects other maps sharing it), recompute hash, `unshare()` (set `cowid = 0`), and if `doWrite`, serialize and persist via `Family::db()`.

- `flushDirty()` → `walkSubTree(backed_, type)`.
- `unshare()` → `walkSubTree(false, ...)`. Used to make everything shareable without writing.

## Common Bug Patterns

- Modifying a node without `unshareNode` first → corrupts every snapshot sharing it.
- Using `std::vector` (instead of `std::deque`) as backing for the `MissingNodes` stack → raw inner-node pointers invalidated on push.
- Processing async fetch completions out of order in `getMissingNodes` → incorrect `setFullBelowGen`, subsequent walks skip incomplete subtrees.
- Inner serialization format mismatch (compressed/full) — branch-count cutoff is 12; deserializer enforces exact sizes and throws `std::runtime_error`.
- `addKnownNode` returning `invalid` is normal (empty branch, hash mismatch) — callers must handle, not assume valid.
- Proof verification with wrong key at any level → false negative; the verifier uses the supplied key's nibbles to pick branches.
- Failing to inspect the `exceptions` vector after `walkMapParallel` — workers swallow `SHAMapMissingNode` to avoid `std::terminate`; missing nodes go into the result vector but the return value reflects exceptions only.
- Mutating a node returned from the `TreeNodeCache` — they have `cowid == 0` by invariant.
- Adding a 5th entry to `TaggedPointer::boundaries` — `static_assert` fails; the 2-bit tag supports exactly 4 values.
- Calling `clone()` with the hash-supplied constructor when the item is also changing — the two-constructor split assumes the item and hash are consistent; pass `hashValid = false` to recompute.

## SHAMapMissingNode Catch Policy

The exception flows up out of `descendThrow` and friends. Catch handlers split into:

- **Recovery** (`LedgerCleaner`, `LedgerMaster`): catch, log at warn, schedule `getInboundLedgers().acquire()`.
- **Fatal/abort** (`RCLConsensus` consensus timer): catch, log at error, `Rethrow()` — crashes the consensus round.
- **Silent failure** (`Ledger.cpp`): catch, return failure — incomplete state tree is treated as invalid ledger.

`SHAMapType` enum values (`TRANSACTION = 1`, `STATE = 2`, `FREE = 3`) are part of the wire protocol — do NOT change.
