# `BuildLedger.h` — Ledger Construction Entry Points

This header declares the two public entry points used to construct a finalized `Ledger` object — the immutable, hash-committed record of one round of consensus. It lives at the boundary between the consensus engine and the ledger state machine: once consensus has agreed on a set of transactions, `buildLedger` is what turns that agreement into a canonical ledger.

## Two Construction Paths

The file exposes two overloads of `buildLedger`, both returning `std::shared_ptr<Ledger>`.

**Consensus path** — the primary production path:

```cpp
std::shared_ptr<Ledger> buildLedger(
    std::shared_ptr<Ledger const> const& parent,
    NetClock::time_point closeTime,
    bool const closeTimeCorrect,
    NetClock::duration closeResolution,
    Application& app,
    CanonicalTXSet& txns,
    std::set<TxID>& failedTxs,
    beast::Journal j);
```

This is called after a consensus round completes. The `txns` parameter is a `CanonicalTXSet` — a sorted, salted map that groups an account's transactions by sequence number to prevent ordering attacks. Critically, `txns` is both an input and an output: transactions that neither succeeded nor definitively failed are left in the set on return, signaling to the caller that they must be re-queued for the next round. `failedTxs` accumulates the IDs of transactions that failed irrecoverably in this round.

**Replay path** — used during ledger replay (e.g., catchup or history validation):

```cpp
std::shared_ptr<Ledger> buildLedger(
    LedgerReplay const& replayData,
    ApplyFlags applyFlags,
    Application& app,
    beast::Journal j);
```

`LedgerReplay` bundles a parent ledger, the ledger being replicated, and its transactions pre-sorted into apply order via `orderedTxns()` (keyed by `uint32_t` sequence position, not account sequence). This path passes caller-controlled `ApplyFlags`, which in practice includes flags like `tapNO_CHECK_SIGN` to skip re-verifying cryptographic signatures on already-validated transactions — an important performance optimization for replay at scale.

## Shared Skeleton: `buildLedgerImpl`

Both overloads delegate to an internal template function `buildLedgerImpl` defined in `detail/BuildLedger.cpp`. The template parameter `ApplyTxs` is a callable with signature `void(OpenView&, std::shared_ptr<Ledger> const&)`, letting the two paths inject their transaction-application logic while sharing the surrounding ledger lifecycle:

1. A new mutable `Ledger` is constructed from the parent and close time.
2. If this is a flag ledger (every 256th ledger), `updateNegativeUNL()` is called to finalize UNL adjustments — a consensus mechanism for disabling offline validators.
3. An `OpenView` accumulator wraps the mutable ledger. All transaction state changes are applied here without immediately committing.
4. `accum.apply(*built)` flushes the accumulated delta back to the ledger's `SHAMap`.
5. The ledger's skip list is updated (used for efficient ledger-hash lookups at arbitrary sequence numbers).
6. Both the account state and transaction `SHAMap` trees are flushed to the node store (`flushDirty`), persisting the new LCL.
7. `unshare()` ensures the ledger's internal state maps are not aliased with any other objects.
8. `setAccepted()` locks in the close time and resolution, marking the ledger as final.

## Multi-Pass Transaction Application (`applyTransactions`)

The consensus path's `ApplyTxs` lambda calls `applyTransactions`, which implements a retry loop bounded by two constants from `OpenLedger.h`: `LEDGER_TOTAL_PASSES = 3` and `LEDGER_RETRY_PASSES = 1`.

The rationale for multiple passes is inter-transaction dependency: an `OfferCreate` from account A may only succeed once a preceding `Payment` to account A (from a different account) is applied first, but the `CanonicalTXSet` ordering might process them in the wrong relative order within one pass. Retry passes allow such dependent transactions to succeed after their prerequisites have been resolved.

The pass structure:
- Passes 0 through `LEDGER_RETRY_PASSES` are "retry" passes — transactions returning `Retry` stay in the set.
- Once `changes == 0` or `pass >= LEDGER_RETRY_PASSES`, the retry flag switches off.
- At least one non-retry "final" pass is guaranteed before the loop exits.
- An `XRPL_ASSERT` enforces the invariant: if `txns` is non-empty at exit, the retry flag must be `false` (confirming the final pass ran).

On pass 0, any transaction already present in the built ledger's transaction map (a duplicate from a previous round) is immediately dropped. This is the deduplication guard — without it, a retry transaction that was already committed could be applied twice.

Exception safety is handled per-transaction: any `std::exception` thrown during `applyTransaction` moves the offending transaction into `failedTxs` and removes it from `txns`, preventing a single bad transaction from aborting the entire ledger build.

## Design Rationale

The overloaded-function design (rather than a strategy object or enum dispatch) is intentional: the two paths have fundamentally different input types (`CanonicalTXSet` vs. `LedgerReplay`) with incompatible interfaces, so overload resolution cleanly separates them without a runtime branch inside the implementation. The `buildLedgerImpl` template avoids code duplication for the ledger lifecycle while remaining zero-cost — the lambda is inlined by the compiler, leaving no virtual dispatch overhead in a hot path.

The `OpenView` accumulation pattern (apply to a view, then flush) is consistent with how `OpenLedger` manages the live open ledger, ensuring that the same apply semantics are used for both speculative (open) and final (closed) ledger construction.