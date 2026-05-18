# SHAMap.cpp

## Role in the System

`SHAMap` is the foundational authenticated data structure of the XRP Ledger. Every ledger snapshot — whether it represents account state or a transaction set — is stored as a `SHAMap`. The structure is simultaneously a 16-way radix trie (fan-out of 16, depth 64) and a Merkle tree: inner nodes hash their children, and the root hash cryptographically commits to the entire content. This dual nature means that two nodes can prove they agree on ledger state simply by comparing root hashes, and any disagreement can be located in O(log N) steps.

This file implements the full lifecycle of a `SHAMap`: construction, mutation (add, delete, update), read access and iteration, copy-on-write snapshotting, lazy node fetching from the database or peers, and flushing dirty nodes back to persistent storage.

---

## State Machine and the `cowid_` Invariant

Every `SHAMap` carries a `state_` (`SHAMapState`: `Modifying`, `Immutable`, `Synching`, `Invalid`) and an integer `cowid_` — a copy-on-write generation counter.

The `cowid_` is the linchpin of the snapshotting system. Tree nodes carry their own `cowid()` indicating which map generation owns them. A node is "owned" by this map when its `cowid()` equals the map's `cowid_`; it is "shared" (canonicalized) when its `cowid()` is zero. `unshareNode()` enforces this: if a node's generation differs from the current map's, it clones the node before any mutation occurs. This is cheaper and more expressive than a single dirty-bit because multiple live snapshots can coexist with independent generation counters without any coordination.

The copy constructor increments `cowid_` (`cowid_(other.cowid_ + 1)`) and then calls `unshare()` if either the original or the new copy is mutable — preventing concurrent maps from accidentally sharing nodes that either side might later mutate. An immutable copy of an immutable map can share all nodes safely without any clone step.

---

## Constructors and Snapshotting

Three constructors handle the three entry points:

- `SHAMap(t, f)` — creates a fresh, empty map in `Modifying` state with a new empty root `SHAMapInnerNode`.
- `SHAMap(t, hash, f)` — creates a map in `Synching` state. The `hash` parameter is intentionally ignored at construction (only the root's hash matters once it is fetched), but is part of the API to signal caller intent clearly.
- `SHAMap(other, isMutable)` — used by `snapShot()`. Copies all metadata and the `root_` pointer, then conditionally calls `unshare()` to break sharing when mutability demands it.

`snapShot()` is a thin wrapper returning a `std::shared_ptr<SHAMap>` — necessary because callers often need to extend the map's lifetime beyond the current scope.

---

## Tree Traversal

`walkTowardsKey(id, stack)` is the core descent primitive. Starting from `root_`, it selects a 4-bit nibble of the 256-bit key at each level (via `selectBranch`), follows that branch, and optionally records each visited node in a `SharedPtrNodeStack`. The stack records the path from root to — but not including — the target node, enabling `dirtyUp()` to walk back up efficiently.

`findKey()` calls `walkTowardsKey()` and performs an exact key comparison at the leaf, returning `nullptr` if the leaf found does not carry the requested key. This is necessary because the radix trie can terminate at a leaf whose prefix matches but whose stored key diverges.

`firstBelow()` and `lastBelow()` delegate to `belowHelper()`, which accepts a tuple of lambdas `{init, cmp, incr}` to parameterize direction — avoiding code duplication. `peekFirstItem()` and `peekNextItem()` build on these to implement the map's forward iterator. `onlyBelow()` answers "is there exactly one leaf under this subtree?" and is used during deletion to collapse the trie.

---

## Mutation: Add, Delete, Update

All three mutation operations share a common pattern: walk towards the target key building a path stack, perform the local structural change, then call `dirtyUp()` to propagate hash invalidation upwards.

**`addGiveItem()`** handles two cases. If the walk terminates at an inner node with an empty branch, it simply creates a typed leaf there. If it terminates at a leaf whose key collides in prefix with the new key, it must split: a loop descends additional levels creating new `SHAMapInnerNode` instances until the two keys diverge into separate branches. This respects the radix trie's merge property — inner nodes are only created when multiple items must coexist below them.

**`delItem()`** removes the leaf and walks back up the stack, reducing each inner node's child count. If an inner node drops to zero children it is nulled out; if it drops to one child and `onlyBelow()` confirms a single item below it, the inner node collapses and the leaf is hoisted up — enforcing the merge property. The replacement leaf is recreated via `makeTypedLeaf()` using the deleted leaf's original type.

**`updateGiveItem()`** locates the leaf, CoW-unshares it, swaps the payload, and — only if `setItem()` signals the hash changed — calls `dirtyUp()`, preventing spurious rehashing for no-op updates.

**`dirtyUp(stack, target, child)`** consumes the path stack bottom-up, calling `unshareNode()` on each inner node and `setChild()` to link in the updated subtree, producing a chain of freshly CoW-owned inner nodes from the modified point back up to the root.

---

## Node Fetching and Backed Maps

`SHAMap` works both fully in-memory and lazily against a `NodeStore` database. The `backed_` flag distinguishes these modes.

Node retrieval follows a tiered strategy in `fetchNodeNT()`:

1. **In-process cache** (`cacheLookup()`) — queries `Family::getTreeNodeCache()`.
2. **Database** (`fetchNodeFromDB()`) — calls `f_.db().fetchNodeObject()`.
3. **Sync filter** (`checkFilter()`) — consults a `SHAMapSyncFilter`, supplying nodes received from peers during ledger acquisition.

`fetchNodeNT()` returns `nullptr` on miss; `fetchNode()` throws `SHAMapMissingNode`. `descendThrow()` uses the throwing variant, ensuring that callers expecting a fully-available tree receive an exception rather than silent `nullptr` propagation.

`finishFetch()` deserializes raw bytes into a `SHAMapTreeNode` via `makeFromPrefix()`, calls `canonicalize()` to register the node in the cache, and catches `std::runtime_error` to log and suppress deserialization failures rather than crash.

`descendAsync()` offers a non-blocking path: if the node is absent from cache and filter, it posts an async I/O request via `f_.db().asyncFetch()` and sets `pending = true`. The callback invokes `finishFetch()` and fires a user-supplied callback. This is used during sync to maximize I/O concurrency across many concurrent node fetches.

---

## Canonicalization and Caching

`canonicalize(hash, node)` registers a node in the family-wide `TreeNodeCache`, or replaces the local pointer with an already-cached equivalent. The node must have `cowid == 0` before this call — only "unshared" nodes are safe to place in the shared cache. `cacheLookup()` asserts that returned nodes have `cowid == 0`, enforcing this invariant at both sides of the cache boundary. This design avoids duplicating node objects across multiple `SHAMap` instances rooted in the same `Family`.

---

## Flushing and Persistence

`walkSubTree(doWrite, t)` performs a post-order depth-first traversal using an explicit stack (safe on a potentially 64-level tree). For each node: `preFlushNode()` clones it if its `cowid` differs from the map's (protecting other maps sharing the node), then leaf and inner nodes compute updated hashes and call `unshare()` (sets `cowid` to 0). If `doWrite` is true, `writeNode()` serializes and persists to `f_.db()`.

`flushDirty()` calls `walkSubTree(backed_, t)`, writing only for database-backed maps. `unshare()` calls `walkSubTree(false, ...)`, traversing to make all nodes shareable without writing.

`getHash()` contains a deliberate `const_cast<SHAMap&>(*this).unshare()` when the root hash is zero. Computing the root hash requires traversing and updating inner node hashes — logically a read, but physically mutating. The `const_cast` is the acknowledged design compromise.

---

## Leaf Type Dispatch

`makeTypedLeaf()` maps `SHAMapNodeType` to one of three concrete leaf classes: `SHAMapTxLeafNode`, `SHAMapTxPlusMetaLeafNode`, `SHAMapAccountStateLeafNode`. Unrecognized types throw `LogicError` immediately — a programming error, not a recoverable condition. The three-way split exists because the XRPL serializes transaction maps and state maps differently, and type information must survive round-trips through the database.

---

## Invariants and Defensive Patterns

`invariants()` forces a full hash recompute, iterates every leaf via `peekFirstItem`/`peekNextItem`, and delegates to the root node's own `invariants()` check. Throughout the file, `XRPL_ASSERT` guards internal preconditions — state checks in `dirtyUp()`, `cowid` invariants in `unshareNode()` and `walkSubTree()`, non-null stack conditions in iteration — surfacing logic bugs in development without runtime overhead in production. Error conditions arising from external input use exceptions (`SHAMapMissingNode`, `LogicError`, `std::runtime_error`).

The `full_` flag tracks whether the map is believed complete in the database. If `finishFetch()` finds a node absent, it clears `full_` and calls `f_.missingNodeAcquireBySeq()`, notifying the acquisition subsystem to re-fetch the ledger — integrating the map's lazy-loading mechanism directly with the ledger acquisition pipeline.