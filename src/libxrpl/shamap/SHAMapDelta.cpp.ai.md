# SHAMapDelta.cpp

This file implements the comparison and completeness-checking operations on `SHAMap`, the hex-radix Merkle trie that underlies every XRPL ledger's account-state and transaction maps. It is logically distinct from the main map mechanics (insert, fetch, hash) and focuses entirely on answering two questions: *how do two trees differ?* and *what nodes are missing from this tree?*

## The Delta Comparison Subsystem

`SHAMap::compare` and `SHAMap::walkBranch` together implement a diff of two maps. The result is a `Delta` — a `std::map<uint256, DeltaItem>` where each entry records a `DeltaItem = pair<intrusive_ptr<SHAMapItem const>, intrusive_ptr<SHAMapItem const>>`. A `nullptr` first element means the item was absent from the first map (added), a `nullptr` second element means it was absent from the second map (deleted), and two non-null pointers at the same key mean the content changed between maps.

### The Core Optimization

The key insight is that `compare` short-circuits at the root hash before touching any node:

```cpp
if (getHash() == otherMap.getHash())
    return true;
```

This is the whole point of a Merkle tree — matching subtrees need not be visited. When `compare` descends into two inner nodes, it iterates their 16 child-hash slots and only pushes a pair onto the work stack when the hashes for that branch differ. Matching branches are silently skipped, giving the algorithm O(d) complexity in the number of differences rather than O(n) in total items.

### The Four Cases in `compare`

At each step the algorithm pops a pair `(ourNode, otherNode)` and dispatches on their types:

1. **Both leaves**: Direct key and slice comparison. Same key but different data → one modified entry; different keys → two unmatched entries (one deleted, one added).
2. **Inner vs. leaf**: The inner subtree must be walked fully against the single leaf item from the other side. Delegated to `walkBranch`.
3. **Leaf vs. inner**: Symmetric, but calls `walkBranch` on `otherMap` with `isFirstMap = false`.
4. **Both inner**: Iterate 16 child slots; push differing non-empty pairs for further traversal; call `walkBranch` when one side is empty.

### `walkBranch` and the `isFirstMap` Flag

`walkBranch` handles the asymmetric case: a full subtree on one side paired against either nothing (null `otherMapItem`) or a single leaf from the other side. It iterates the subtree breadth-first using an explicit stack, descending inner nodes and collecting all leaf items. For each leaf it finds three outcomes:

- The other side has no item at all (`emptyBranch`): the leaf is recorded as unmatched.
- The leaf key matches `otherMapItem->key()` but slices differ: a modified-item entry is recorded and `emptyBranch` is set to true (the item has been "consumed").
- Keys match exactly: a perfect match; `emptyBranch` is set true to suppress a trailing unmatched entry.

After the walk, if `otherMapItem` was never matched (because its key didn't appear anywhere in the subtree), it is added as its own unmatched entry.

The `isFirstMap` boolean determines which half of the `DeltaItem` pair carries the item and which carries `nullptr`, ensuring that the semantic meaning — (first-map version, second-map version) — is preserved regardless of which direction the asymmetry runs.

### The `maxCount` Defense

Both functions accept `maxCount` by reference and decrement a single shared counter on every insertion into `differences`. The caller receives `false` when the limit is reached, indicating a truncated diff. This guards against pathological or adversarial cases where a peer advertises a tree with thousands of fabricated differences — without a limit, even a short sync handshake could trigger O(n) traversal. The `compare` public interface takes `maxCount` by value, then passes it by reference into `walkBranch`, so the counter is shared across all recursive delegations.

The two primary callers reveal how the limit is used:
- The ledger-diff RPC (`LedgerDiff.cpp`) passes `std::numeric_limits<int>::max()`, treating it as unlimited.
- The ledger-sync handler (`Ledger.cpp` RPC) passes a small constant like 256, bounding exposure during synchronization.

## Missing-Node Detection

`walkMap` and `walkMapParallel` serve a different need: checking that the local node store contains every node of a given tree. During ledger loading, a node may have a hash for a child but not the child's data. `descendNoStore` (called from both functions) returns null for such gaps without throwing, so the caller can aggregate them into a `std::vector<SHAMapMissingNode>` and report or fetch them.

`walkMap` is a straightforward iterative DFS over inner nodes: descend each non-empty branch, push inner nodes, and record nulls. It is used for transaction maps and as the fallback for state maps.

### Parallel Walk Strategy

`walkMapParallel` exists for performance during ledger loading and is called from `Ledger.cpp` when the parallel flag is set. It partitions the tree at depth 1 — the 16 children of the root inner node — and launches one `std::thread` per non-empty, non-leaf top-level child. Each thread owns its own `nodeStack` and traverses its subtree independently. Only `missingNodes` and `maxMissing` are shared between threads, protected by a single `std::mutex` via `std::lock_guard`.

A subtle correctness concern: an unhandled exception in a `std::thread` calls `std::terminate`. The worker lambda therefore catches `SHAMapMissingNode` and records it into a separate `exceptions` vector (also mutex-guarded) rather than letting it escape. After joining all threads the function inspects this vector and logs each caught exception. The return value — `true` if no exceptions were thrown — is semantically distinct from whether missing nodes were found; missing nodes are always communicated through the output vector.

The choice to parallelize at depth 1 rather than deeper is a pragmatic tradeoff: it gives up to 16-way parallelism with zero dynamic load balancing, trades the simplicity of one shared stack for 16 independent stacks, and avoids any coordination overhead during descent.