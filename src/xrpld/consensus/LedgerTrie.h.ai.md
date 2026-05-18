# `LedgerTrie.h` — Compressed Ancestry Trie for Consensus Support Tracking

## Purpose and Role

`LedgerTrie.h` implements a compressed trie (prefix tree) data structure that tracks how much validator support each ledger in the XRPL network has received. It is the core data structure powering the "preferred ledger" calculation in XRPL's consensus algorithm, living inside the broader `Validations<>` template in `Validations.h`, which holds a `LedgerTrie<Ledger> trie_` member and feeds validator observations into it.

The fundamental insight the trie exploits is that ledger history is a **string over the alphabet of ledger IDs**: a ledger at sequence `N` implicitly defines a length-`N` string where the `i`-th character is the ID of its ancestor at sequence `i`. Two ledgers that share ancestry share a common prefix of this string. A compressed trie over these strings groups validators onto shared ancestry branches efficiently, making it possible to ask "which branch of history has the most support right now?" in a single tree traversal rather than by scanning all validators.

## Class Hierarchy

### `SpanTip<Ledger>`

A lightweight value type carrying the sequence number and ID of the tip of a span, plus a copy of the ledger itself so that ancestor IDs at earlier sequence numbers can be looked up via `ancestor(Seq s)`. This avoids repeatedly reaching into the ledger object from call sites that just need to identify a position in the trie. `SpanTip` is the return type of `getPreferred()` — the consensus engine receives it as the resolved preferred ledger.

### `ledger_trie_detail::Span<Ledger>`

A `Span` represents a contiguous half-open interval `[start_, end_)` of ledger sequence numbers, backed by a single `Ledger` instance from which ancestor IDs can be retrieved. The key operations are `diff()` — which delegates to the free function `mismatch(ledger_, other)` to find the first sequence where histories diverge — and the slicing operations `from(spot)` and `before(spot)` that return sub-spans. The `merge()` free friend function combines two overlapping spans by taking the endpoints from the one with the higher sequence number, since that ledger necessarily knows its own ancestry further back.

This design cleanly separates "what ledger IDs exist at each position" (owned by the `Ledger` value inside the span) from "which range of positions this node covers" (the `[start_, end_)` pair). Spans themselves are cheap to copy because `Ledger` is documented to be lightweight.

### `ledger_trie_detail::Node<Ledger>`

A trie node owns a `Span`, two support counters, a `parent` raw pointer, and a `std::vector<std::unique_ptr<Node>>` children list. The ownership model is strictly top-down: each node owns its children but holds only a raw non-owning pointer to its parent.

`tipSupport` is the number of current validations whose exact ledger matches the tip of this node's span. `branchSupport` is `tipSupport` plus the sum of all descendants' `branchSupport` — it counts every validator that has validated this ledger or any of its descendants. These two counters are maintained incrementally on every insert and remove by walking up the parent chain. The `erase(child)` method uses a swap-and-pop idiom to remove a child in O(1).

## `LedgerTrie<Ledger>` — The Trie Itself

### Compression Invariant

The trie maintains a strict structural invariant: **any non-root node with zero `tipSupport` must have at least two children**. A node with zero tip support and exactly one child is redundant — it can be merged with its child with no loss of information. Both `insert()` and `remove()` actively enforce this.

The root node is exempt from this invariant and represents the genesis ledger; it always exists even when the trie is logically empty (checked via `root->branchSupport == 0`).

### `insert(ledger, count)`

Insertion first calls `find(ledger)`, which walks the trie to locate the node with the longest common prefix with the incoming ledger. The difference point `diffSeq` identifies where the incoming ledger diverges from the found node's span. The logic then handles up to two structural modifications:

1. **Split** (`oldSuffix` exists): The found node `loc` shares only a prefix with the new ledger. The suffix of `loc`'s span is extracted into a new child node, which inherits `loc`'s existing children and support counts. `loc` is truncated to the prefix, with `tipSupport` set to 0. This is the classic compressed-trie prefix split.

2. **Branch** (`newSuffix` exists): A new leaf node for the remainder of the incoming ledger is appended as a new child.

After structural surgery, `tipSupport` is incremented on the target node and `branchSupport` is propagated up to the root via the parent chain. `seqSupport[ledger.seq()]` is also incremented — this per-sequence-number index is crucial for the preferred-ledger algorithm.

### `remove(ledger, count)`

Removal uses `findByLedgerID()` (an O(n) exact-match search) rather than the prefix-based `find()`, because tip support must be decremented from an exact ledger node, not just the best prefix match. After decrementing, `branchSupport` is decremented up the parent chain. The compression step then walks up from the removed node: leaf nodes with zero `tipSupport` are deleted, and nodes with zero `tipSupport` and exactly one child are merged with that child using `merge()`.

### `getPreferred(largestIssued)`

This is the most algorithmically significant method. It answers: *given the current distribution of validator support, which specific ledger should this node work toward?*

The algorithm walks the trie from the root, at each step applying a **"preferred by branch"** rule. The key concept is **uncommitted support**: validators whose last issued validation is at a sequence number smaller than the current frontier haven't yet expressed a preference for the current generation of ledgers. Because they *might* validate any branch, they are potential support for competing branches and must be treated as a threat to the current best candidate.

`seqSupport` is a `std::map<Seq, uint32_t>` that counts how many validators have their tip at each sequence number. The algorithm iterates `seqSupport` entries in order, accumulating validators who are "behind" the current position into the `uncommitted` counter. A branch is considered safe to advance into only when its `branchSupport` exceeds `uncommitted` — i.e., even if every uncommitted validator piled onto any other branch, the current best branch would still win.

Within a single node's span (where there is no branching), the algorithm advances sequence by sequence, checking each `seqSupport` entry. When advancing past the end of a node's span, it picks among children by `branchSupport`, with `startID()` as a deterministic tie-breaker. A child is chosen only if its `branchSupport` lead over the second-best child exceeds `uncommitted` (with the tie-breaker winner getting one extra margin point). If no child clears the bar, the current node's tip is returned as the preferred ledger.

This conservative strategy — requiring a margin *strictly greater than* uncommitted validators — is intentional. It prevents thrashing between competing branches when validators are slow to report, a realistic condition in a distributed network.

### `checkInvariants()`

A debugging and testing method that performs a full DFS of the trie, verifying: (1) no non-root zero-tip-support node has fewer than two children; (2) every node's `branchSupport` equals its `tipSupport` plus the sum of its children's `branchSupport`; (3) parent pointers are correct; and (4) the `seqSupport` map matches the sum of `tipSupport` values grouped by sequence number. The consensus framework test suite calls this after every mutation.

## Integration with `Validations`

`Validations.h` uses `LedgerTrie` through a `withTrie()` accessor that flushes stale validations before delegating to a lambda. Validations are inserted via `updateTrie()` when a validator's ledger is acquired, and removed via `removeTrie()` when they expire. The `getPreferred()` call passes `localSeqEnforcer_.largest()` — the largest sequence number this node itself has validated — as the `largestIssued` anchor, ensuring that validations behind the local frontier count as uncommitted. `branchSupport() - tipSupport()` is also used directly to count validators who are descendants of a given ledger (i.e., have moved past it), used when computing child counts for consensus decisions.

## Ledger Template Contract

The `Ledger` type must be cheap to copy, provide `seq()`, support indexed ancestor lookup via `operator[](Seq)` returning `ID{0}` for unknowns, construct a genesis ledger via a tag type `MakeGenesis{}`, and participate in a free function `mismatch(Ledger, Ledger)` that returns the first sequence number where the two ledgers may differ. The **unique history invariant** — that agreement on any ancestor ID implies agreement on all earlier ancestors — is what makes the trie representation sound: a ledger history truly is a string in the formal sense, with no branching within a single ledger's ancestry chain.