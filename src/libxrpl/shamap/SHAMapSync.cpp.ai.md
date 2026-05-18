# SHAMapSync.cpp

This file implements the network synchronization engine for the XRPL `SHAMap` — the combined 16-way radix/Merkle tree that underpins ledger state and transaction sets. While other files in the `shamap` module handle in-memory mutations and local lookups, `SHAMapSync.cpp` answers the specific question that drives the consensus protocol: *"What does a peer need to send me, or what must I send a peer, to bring a SHAMap into agreement?"* It also provides general traversal utilities used by both sync code and application logic.

## Tree Traversal

`visitNodes` implements a depth-first traversal using an explicit `std::stack` rather than recursion, avoiding stack exhaustion on deep maps. It feeds every tree node, inner and leaf alike, through a caller-supplied `bool`-returning functor; returning `false` exits early. The traversal skips empty branches and uses a deliberate optimization: it avoids pushing an inner node onto the stack if that node has no remaining children to explore, saving pointless push/pop cycles. `visitLeaves` is a thin wrapper that delegates to `visitNodes` and filters to leaf nodes only.

`visitDifferences` compares two maps efficiently by leveraging the Merkle property. It short-circuits immediately if the two root hashes match — making it O(1) when maps are identical — and then uses `hasInnerNode` to prune entire subtrees that are already in agreement, visiting only nodes not present in the reference map. The `have` pointer is nullable; a null value means "report everything in `this`," which covers the bootstrap case where the peer has nothing yet.

## Missing Node Discovery

`getMissingNodes` is the most complex function in the file. Its job is to traverse the local map, discover which nodes are referenced but unavailable locally, and return up to `max` such (nodeID, hash) pairs. This is called repeatedly during ledger synchronization until the result is empty.

The complexity comes from two competing demands: correctness across async I/O and throughput for heavily distributed sync. The function uses the `MissingNodes` inner struct (defined in `SHAMap.h`) to bundle all traversal state — the work stack, the set of already-discovered missing hashes, the deferred-read queue, and a "resume" map.

**Random traversal start.** `firstChild` in each `StackEntry` is initialized to `rand_int(255)`, so multiple threads or peers calling `getMissingNodes` concurrently on the same map will produce different request sets. This is intentional: sending different requests increases the probability of acquiring distinct missing nodes per round, rather than redundantly requesting the same nodes.

**FullBelow cache.** The `Family` object holds a generational "full below" cache that remembers which subtree roots have been confirmed locally complete. Before descending into any child, the code checks this cache via `touch_if_exists`. After fully processing an inner node with no missing children, it writes the node's hash into the cache and marks it with the current generation via `setFullBelowGen`. The generation number lets the cache age out stale entries across ledger boundaries.

**Async I/O.** `descendAsync` attempts to fetch a child node from storage without blocking. If the read is pending, the caller (via a lambda) will be notified asynchronously, and `mn.deferred_` is incremented. When the deferred limit (`maxDefer_`, hardcoded to 512) is reached, `gmn_ProcessDeferredReads` is called. This function blocks on a `std::condition_variable`, draining all in-flight reads before resuming traversal. Nodes that complete successfully are canonicalized into the tree and added to the `resumes_` map, so the parent is revisited once the async batch is done.

The `MissingNodes::stack_` uses `std::deque` as its underlying container rather than the default `std::vector`. This is explicitly required: raw `SHAMapInnerNode*` pointers stored in stack entries must remain valid while new entries are pushed. `std::vector` can reallocate and invalidate those pointers; `std::deque` does not.

Two private helpers separate concerns: `gmn_ProcessNodes` handles the iterative descent and bookkeeping for a single inner node, while `gmn_ProcessDeferredReads` handles I/O completion. Both mutate the same `MissingNodes` context object.

## Serving Nodes to Peers

`getNodeFat` serves a requested node plus nearby descendants in a single response. The "fat" protocol amortizes round-trip latency during sync: instead of exchanging one node per message, a server bundles a subtree up to a specified depth. The depth budget is decremented only when an inner node has more than one child; single-child chains ("compressed paths" in the radix sense) are traversed for free. This means a server doesn't waste depth budget traversing structurally forced paths that carry no branching information. If `fatLeaves` is true, leaf nodes adjacent to the traversal boundary are included; otherwise only inner nodes are bundled.

## Ingesting Nodes During Sync

`addRootNode` and `addKnownNode` are the receiving side of the sync protocol. Both return a `SHAMapAddNode` result — a tristate of `useful` (node successfully integrated), `duplicate` (node already present), or `invalid` (node corrupt or structurally inconsistent) — letting the caller accumulate statistics across a batch of received nodes.

`addKnownNode` performs two levels of integrity validation. The first is hash verification: the deserialized node must hash to the expected value stored in its parent branch. The second is structural: for leaf nodes, the function reconstructs the expected `SHAMapNodeID` from the leaf's actual key and verifies it matches the `SHAMapNodeID` the caller claimed. A hash collision could in theory produce a blob that hashes correctly but belongs at a different tree position; this check closes that gap. A mismatched depth or position at `leafDepth` transitions the map to `SHAMapState::Invalid`, which is the appropriate response to provably corrupt data — not a crash, but a state that the consensus engine can detect and act on. The function also skips descending into FullBelow subtrees, consistent with how `getMissingNodes` tracks completeness.

## Merkle Proof Generation and Verification

`getProofPath` collects every node on the path from a given leaf to the root by calling `walkTowardsKey` with a stack, then serializing each node in leaf-to-root order. The reverse ordering (leaf first) is significant: the verifier processes root first and walks down toward the leaf, matching hashes along the way.

`verifyProofPath` is a static method — it operates on raw serialized bytes and requires no live SHAMap. It deserializes each blob in root-to-leaf order, verifies the hash matches the expected value, then selects the child hash for the next step using `selectBranch`. The entire deserialization is wrapped in a try/catch because the path data originates from the network and may be malformed. The function also enforces a path length bound of 65, matching the maximum tree depth (64 inner levels plus one leaf).

## Relationship to the Broader Module

All traversal paths ultimately call `descendThrow` or `descendNoStore` — functions defined elsewhere in the module that handle cache lookups, database fetches, and the copy-on-write protocol. This file is deliberately free of those details; it focuses on tree-level logic and assumes the descent primitives provide correctly typed, non-null nodes or throw on failure. The `SHAMapSyncFilter` interface is the seam between this code and the application layer: it lets the caller inject an alternative node source (e.g., in-progress relay data) and receive notifications when nodes are integrated, without this file depending on any specific storage backend.