# `NegativeUNLVote.cpp` — Negative UNL Voting Logic

## Purpose and Context

The Negative UNL (N-UNL) is a XRPL consensus feature that allows the network to maintain liveness when a subset of trusted validators goes offline. When enough validators agree that a peer validator has stopped participating, they can flag it in a special ledger structure. From that point forward, quorum calculations exclude the flagged validator — the network continues to make progress without waiting for it to return. This file implements `NegativeUNLVote`, the class that drives that decision: scoring validators on recent participation, identifying candidates for disabling or re-enabling, and injecting the corresponding `ttUNL_MODIFY` pseudo-transactions into the consensus round.

## When Voting Fires

`doVoting()` is called from `RCLConsensus::Adaptor::buildInitialSet()` specifically when `prevLedger->isVotingLedger()` returns true — i.e., when the previous ledger is at position `seq % 256 == 255`, one ledger before a flag ledger. This means N-UNL changes land in flag ledgers themselves (multiples of 256). Fee votes and amendment votes fire one ledger later, when `prevLedger->isFlagLedger()` is true. The separation ensures that by the time fee/amendment voting runs, the N-UNL for that flag ledger is already resolved.

## Building the Score Table

`buildScoreTable()` is the measurement phase. It reads the ancestor skip list from the previous ledger (`keylet::skip()`), which efficiently stores the hashes of the prior 256 ledgers. It then queries `RCLValidations::getTrustedForLedger()` for each of those ledger hashes, incrementing a counter for each UNL validator that submitted a trusted validation.

Two guards prevent voting on stale or skewed data:

1. **Insufficient history**: If the skip list contains fewer than `FLAG_LEDGER_INTERVAL` (256) entries, the ledger chain is too short to score reliably. The function returns an empty optional, and `doVoting()` silently skips the round.

2. **Local node participation check**: Before returning the score table, the function checks whether the local node itself validated at least `negativeUNLMinLocalValsToVote` (90% of 256 ≈ 230) of those ancestor ledgers. If the local node was itself offline or lagging, its view of other validators' participation is unreliable — it may have simply missed validations that the rest of the network received. Returning `{}` here is the conservative correct choice: a node that can't vouch for its own recent history shouldn't be influencing which validators get flagged as unreliable.

The function also instructs the validation container to retain history: `validations.setSeqToKeep(seq - 1, seq + FLAG_LEDGER_INTERVAL)` ensures the next voting round has the data it needs, even as old validation messages are normally pruned.

## Finding Candidates

`findAllCandidates()` applies two watermarks to the score table:

- **To disable**: A validator with fewer than `negativeUNLLowWaterMark` (50% of 256 = 128) validations is considered persistently offline. To be a disable candidate it must also: not already appear in the N-UNL, not be a new validator (guarded by `newValidators_`), and the current N-UNL must have room (capped at 25% of the UNL via `negativeUNLMaxListed`).

- **To re-enable**: A validator already in the N-UNL that has recovered and delivered more than `negativeUNLHighWaterMark` (80% of 256 ≈ 204) validations becomes a re-enable candidate. The asymmetric thresholds (50% to enter, 80% to exit) create deliberate hysteresis — a flaky validator that sits near 50% doesn't oscillate in and out of the N-UNL.

There is a special second pass for re-enable candidates: if a validator has been removed from *all* nodes' UNLs but still appears in the N-UNL, it will never accumulate validations (it's no longer trusted). `findAllCandidates()` catches this by adding any N-UNL member that is absent from the current UNL as a re-enable candidate, cleaning up the ledger state.

Importantly, the function projects the "next" N-UNL by applying any pending changes from the previous ledger's `validatorToDisable` and `validatorToReEnable` fields before running the candidate search. This avoids double-counting transitions that are already in-flight.

## Deterministic Selection

If multiple validators qualify as candidates, `choose()` selects exactly one. It XORs every candidate's `NodeID` against the first 20 bytes of the parent ledger's hash, treating the result as an unsigned integer, and picks the minimum. All validating nodes see the same parent ledger hash, so all honest nodes pick the same candidate and propose the same `ttUNL_MODIFY` transaction — this is how the pseudo-transaction achieves consensus without an explicit proposal round.

## Injecting the Pseudo-Transaction

`addTx()` constructs an `STTx` of type `ttUNL_MODIFY`, serializes it, and inserts it into the `initialSet` SHAMap as a `tnTRANSACTION_NM` (non-malleable transaction) item. The transaction carries three fields: a disable/re-enable flag (`sfUNLModifyDisabling`), the target ledger sequence (`sfLedgerSequence`), and the validator's master public key (`sfUNLModifyValidator`). Because all nodes independently derive the same transaction, it naturally reaches consensus as part of the ledger's transaction set without being submitted via the normal transaction queue.

## New Validator Protection

`newValidators_` is a `hash_map<NodeID, LedgerIndex>` tracking validators that were recently added to the trust list. A validator is immune from disabling for two full flag periods (`newValidatorDisableSkip = 512 ledgers`) after it first appears as trusted. This protects a legitimately new validator that simply hasn't had time to accumulate a history. `newValidators()` is called from the consensus adaptor when trust-set changes are detected; `purgeNewValidators()` is called at the start of each `doVoting()` call to evict entries that have aged out.

Both `newValidators()` and `purgeNewValidators()` hold `mutex_` during their mutations. However, `findAllCandidates()` reads `newValidators_` without the lock. This is safe in practice because `doVoting()` calls `purgeNewValidators()` (under the lock) immediately before `findAllCandidates()`, and `doVoting()` itself runs on the consensus thread — the same thread that would be disrupted by any concurrent N-UNL state change. The `newValidators()` path, if called from a different thread, could race with the `findAllCandidates()` read, but the consequence would be benign: at worst, a newly trusted validator would be considered for disabling one round earlier than intended.