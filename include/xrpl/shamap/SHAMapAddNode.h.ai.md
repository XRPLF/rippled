# `SHAMapAddNode.h` — Node-Sync Result Accumulator

`SHAMapAddNode` is a small value-type accumulator used to report and aggregate the outcomes of adding nodes to a `SHAMap` during ledger synchronization. When a rippled node is catching up to the network, it requests raw trie nodes for transaction sets and account state maps from its peers. Each received node is either new and valid ("useful"), already known ("duplicate"), or corrupt/unexpected ("invalid"). This class collects those three counts so callers can assess the quality of a peer's response.

## Design as Named Return Values

The most notable design choice is the set of private-constructor static factory methods: `useful()`, `invalid()`, and `duplicate()`. These construct single-count instances (`mGood=1`, `mBad=1`, and `mDuplicate=1` respectively) and are used directly as return values from `SHAMap::addRootNode()` and `SHAMap::addKnownNode()`. This pattern gives the calling code self-documenting clarity:

```cpp
if (!node || node->getHash() != hash)
    return SHAMapAddNode::invalid();
// ...
return SHAMapAddNode::useful();
```

The alternative — returning a plain enum or boolean — would lose the ability to accumulate multiple outcomes, which matters in `InboundLedger::receiveNode()` where a batch of nodes is processed and the aggregate `SHAMapAddNode` is passed by reference, incremented incrementally via `incInvalid()`, `incUseful()`, and `incDuplicate()`, then inspected once at the call site.

## The `isGood()` vs `isUseful()` Distinction

The two boolean queries encode different questions. `isUseful()` answers "did we receive at least one new node we didn't already have?". `isGood()` answers "was this exchange not net-harmful?" — its implementation is `(mGood + mDuplicate) > mBad`. Duplicates count on the positive side because receiving something you already know is benign; it's not evidence of a misbehaving peer. Only `mBad` accumulates evidence of corruption or protocol violations. `TransactionAcquire::takeNodes()` uses `isGood()` as the gate on whether to continue processing a peer's contribution, while `isUseful()` signals whether the sync actually made forward progress.

## Aggregation via `operator+=`

`operator+=` combines two `SHAMapAddNode` instances by summing all three counters. This lets the ledger acquisition code build a running tally across a batch or across multiple calls, then log or evaluate the aggregate. The `get()` method formats a human-readable string (e.g., `"good:3 dupe:1"`) used in debug-level journal output when nodes arrive.

## Header-Only, All Inline

Every method is `inline` in the header. The class has no dependencies beyond `<string>` and carries no heap allocations or virtual dispatch. This makes it safe and cheap to construct on the stack, pass by value, and return from functions without any concern about overhead — which is appropriate for a type that exists purely to carry a small status payload up the call stack.

## Summary of Semantics

| Counter | Incremented by | Meaning |
|---------|---------------|---------|
| `mGood` | `incUseful()` | Node was new and hash-verified |
| `mBad` | `incInvalid()` | Node was corrupt, hash-mismatched, or structurally wrong |
| `mDuplicate` | `incDuplicate()` | Node was valid but already present |

The class sits at the boundary between the low-level `SHAMap` trie operations and the higher-level peer-reputation / acquisition-progress logic in `InboundLedger` and `TransactionAcquire`, providing a compact, composable status token that flows upward through the sync protocol machinery.