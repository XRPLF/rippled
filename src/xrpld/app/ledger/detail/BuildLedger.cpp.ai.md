# `BuildLedger.cpp` — Ledger Construction from Consensus and Replay

This file implements the mechanics of constructing a new closed ledger on the XRP Ledger. It provides the two public entry points declared in `BuildLedger.h` — one for the consensus path (building a ledger from a fresh set of agreed-upon transactions) and one for replay (deterministically re-deriving a previously-validated ledger). Both paths share a single private implementation template, `buildLedgerImpl`, which handles the invariant bookkeeping surrounding transaction application.

## Role in the Ledger Pipeline

After the consensus protocol agrees on a transaction set, the node must turn that agreement into a concrete `Ledger` object whose state tree hash can be compared with peers. That is the job of the consensus `buildLedger` overload. The replay overload serves ledger acquisition: when a node downloads a historical ledger to fill a gap, it must re-execute its transactions against the known parent to verify the resulting hash rather than trust the network blindly. The two paths demand different application strategies, but the surrounding ceremony — creating the child ledger, staging changes in a view, flushing the SHAMap to storage, and finalizing — is identical, which is why `buildLedgerImpl` exists as a template.

## `buildLedgerImpl` — The Shared Scaffolding

The template accepts an `ApplyTxs` callable with the signature `void(OpenView&, std::shared_ptr<Ledger> const&)`. This design separates the "what transactions to apply and how" from the "how to set up and finalize the ledger," letting the two callers supply only the logic that differs between them.

The sequence inside `buildLedgerImpl` is carefully ordered:

1. **Child ledger creation**: `std::make_shared<Ledger>(*parent, closeTime)` copies the parent's state tree header but does not duplicate the underlying SHAMap nodes — they are shared copy-on-write.

2. **Flag ledger handling**: If the new ledger falls on a flag ledger boundary (sequence divisible by 256), `updateNegativeUNL()` is called. This is the mechanism for updating the set of validators that are currently offline and should be excluded from quorum calculations.

3. **Accumulator pattern**: An `OpenView accum(&*built)` is constructed. `OpenView` is an in-memory staging layer that buffers state changes without writing them to the underlying ledger. The `XRPL_ASSERT(!accum.open(), ...)` immediately after construction confirms the view represents a *closing* (not open-for-new-transactions) ledger — a subtle but important invariant distinguishing in-progress consensus rounds from ledger-building. The `applyTxs` callable receives this accumulator; only after all transactions are processed does `accum.apply(*built)` commit the changes atomically to the ledger.

4. **SHAMap persistence**: `built->stateMap().flushDirty(hotACCOUNT_NODE)` and `built->txMap().flushDirty(hotTRANSACTION_NODE)` write all modified SHAMap nodes to the node store. The heat hints (`hotACCOUNT_NODE`, `hotTRANSACTION_NODE`) influence caching priority in the node store. This step must follow the apply so that only the final, committed state is persisted.

5. **Finalization**: `built->unshare()` breaks sharing with the parent's SHAMap to ensure the new ledger owns its own copy. The fee structure assertion — `built->header().seq < XRP_LEDGER_EARLIEST_FEES || built->read(keylet::fees())` — then guards that every ledger after the early genesis period carries a `FeeSettings` object. Finally, `setAccepted()` stamps the ledger with its close time and resolution, transitioning it to the accepted state.

## `applyTransactions` — The Consensus Retry Loop

The consensus path requires more sophistication than simple ordered application because transactions in a `CanonicalTXSet` can have inter-dependencies. A payment that fails because the sender's sequence number hasn't been advanced yet by a prior transaction should be retried, not discarded.

`CanonicalTXSet` sorts transactions by `(salted_account_key, seqProxy, txid)`, keeping each account's transactions in sequence order while randomizing the relative ordering between accounts (using the parent ledger hash as a salt, preventing adversarial transaction ordering attacks). The retry loop runs at most `LEDGER_TOTAL_PASSES` (3) times, with `LEDGER_RETRY_PASSES` (1) "certain retry" passes and then non-retry "final" passes. These constants are defined as macros in `OpenLedger.h` and mirror the identical logic in `OpenLedger`'s `apply()` function for the open-ledger path.

On each pass, `applyTransaction` is called for each remaining transaction. The result drives three outcomes: `Success` removes the transaction from the set and increments the change counter; `Fail` moves it to the `failed` set and removes it from the working set; `Retry` leaves it in place for the next pass. Once a pass completes with zero changes, the loop either exits (if already in non-retry mode) or switches off `certainRetry`. The final `XRPL_ASSERT` enforces that if any transactions remain after the loop, at least one non-retry pass has occurred — a guarantee that no transaction was simply skipped due to premature loop exit.

Pass 0 has a special short-circuit: if a transaction already exists in the parent ledger (detectable via `built->txExists(txid)`), it is silently dropped. This prevents applying the same transaction twice when building from an ancestor that already captured it.

Exception handling wraps each `applyTransaction` call. If a transaction throws a `std::exception`, it is logged, added to `failed`, and the loop continues. This ensures one malformed transaction cannot abort the construction of an entire ledger.

## Replay Path — Deterministic Re-execution

The replay `buildLedger` overload passes a simpler lambda to `buildLedgerImpl`. It iterates `replayData.orderedTxns()` — a `std::map<uint32_t, shared_ptr<STTx>>` keyed by the transaction's original position in the ledger — and calls `applyTransaction` with `certainRetry = false`. There is no retry loop, no failure set, and no deduplication check. A previously-validated ledger's transaction set is already known to be applicable in order; replaying it is a mechanical repetition, not a consensus negotiation.

The close time correctness flag is derived from the original ledger's close flags: if `sLCF_NoConsensusTime` is *not* set, consensus agreed on the close time and `closeTimeCorrect` is `true`. This bit-level reading of the stored close flags preserves the original ledger's semantics during replay.

## Relationship to Sibling Files

`OpenLedger.h` defines the `LEDGER_TOTAL_PASSES` and `LEDGER_RETRY_PASSES` constants and contains a near-identical retry loop used for the open ledger (transactions arriving during consensus). The two loops are deliberate parallels: the open-ledger path retries newly submitted transactions against the current open view, while `applyTransactions` here retries them against the view being built for the next closed ledger. `LedgerReplay` is a simple value type holding `parent_`, `replay_`, and `orderedTxns_` — its only role is carrying the data the replay lambda needs. `CanonicalTXSet` provides the sorted-by-account transaction container that makes the multi-pass retry strategy effective for the consensus path.