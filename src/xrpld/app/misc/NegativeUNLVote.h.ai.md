# `NegativeUNLVote.h` — Validator Reliability Scoring and Negative UNL Vote Management

## Purpose and Context

The Negative UNL (Unique Node List) is an XRPL protocol mechanism for handling temporarily unreliable validators without destabilizing consensus quorum requirements. When a validator goes offline or becomes unreliable, its absence forces the network to reach a higher fraction of the *remaining* validators to make consensus, potentially stalling the ledger. The Negative UNL lets the network collectively agree to exclude a validator from quorum calculations while it remains offline, effectively treating it as absent rather than absent-but-counted.

`NegativeUNLVote.h` declares the `NegativeUNLVote` class, the singleton manager that implements local vote casting for Negative UNL changes. Once per flag ledger boundary (every `FLAG_LEDGER_INTERVAL` = 256 ledgers), the class scores all validators' recent participation, identifies candidates to disable or re-enable, and injects `ttUNL_MODIFY` pseudo-transactions into the candidate transaction set for the upcoming flag ledger. Because every honest node runs the same deterministic algorithm against the same shared data, they all produce the same pseudo-transaction and it achieves consensus without an explicit vote-collection round.

## Key Design Decisions

### Water Mark Thresholds

The class defines four static constants that govern the entire scoring logic:

- `negativeUNLLowWaterMark` = 50% of 256 (128 validations) — a validator sending fewer than 128 validations in the last flag period is a candidate to be disabled.
- `negativeUNLHighWaterMark` = 80% of 256 (204 validations) — a disabled validator must exceed this to be re-enabled, creating deliberate hysteresis so a flapping validator isn't toggled on every cycle.
- `negativeUNLMinLocalValsToVote` = 90% of 256 (230 validations) — the local node itself must have issued at least this many validations before it participates in the vote. A node that was itself offline cannot reliably measure others and should abstain.
- `negativeUNLMaxListed` = 25% — at most a quarter of the UNL may be on the Negative UNL simultaneously, preventing the protocol from hollowing out quorum entirely.

The wide gap between the low and high watermarks (50% vs 80%) is intentional hysteresis. Without it, a validator sitting around the threshold could oscillate and cause repeated toggle transactions.

### Deterministic Candidate Selection via `choose()`

When `findAllCandidates()` returns multiple validators qualifying for disable or re-enable, only one is acted on per cycle. Rather than a separate voting round, `choose()` XORs each `NodeID` with the first 20 bytes of the previous ledger's hash (used as a random pad), then picks the candidate whose XOR result is numerically smallest. Since every node has the same parent ledger hash, they all pick the same winner — a lightweight leader-election idiom common in the XRPL codebase. A single transaction per cycle is also a rate-limiter: the protocol processes at most one disable and one re-enable per flag period.

### New Validator Grace Period

Adding a validator to the UNL and then immediately marking it offline because it has no recent scoring history would be harmful. The `newValidators_` map (a `hash_map<NodeID, LedgerIndex>`) records when each newly trusted validator was observed, and `findAllCandidates()` excludes them from disable candidates until `newValidatorDisableSkip` ledgers (512 = two full flag periods) have elapsed. `purgeNewValidators()` removes entries from the map when they age out, called at the start of each `doVoting` invocation.

This is the only state that requires mutex protection. `doVoting()` is called on the consensus thread while `newValidators()` may be called from the validator-list update path; the `mutex_` guards only `newValidators_` accesses.

### Score Table Construction and Validation History

`buildScoreTable()` queries `RCLValidations` for every ledger in the last 256 slots, counting how many trusted validators validated each one. Before querying, it calls `validations.setSeqToKeep()` to pin the validation history window, preventing the container from garbage-collecting messages that are still needed for this calculation. The score table maps `NodeID → uint32_t` (validation count). If the ledger lacks 256 ancestors in its skip list (i.e., the chain is too young), the function returns `std::nullopt` and voting is skipped entirely for that cycle.

### NegUnl Projection for Candidate Evaluation

`doVoting()` does not use the ledger's current `negativeUNL()` set verbatim. Instead it projects one step ahead: it inserts `validatorToDisable` (if pending) and removes `validatorToReEnable` (if pending) to compute what the Negative UNL will be *after* the current flag ledger closes. This projection ensures `findAllCandidates()` doesn't re-nominate a validator that is already queued for action.

### Pseudo-Transaction Injection

`addTx()` constructs a `STTx` of type `ttUNL_MODIFY`, sets `sfUNLModifyDisabling` to 1 or 0, records the ledger sequence and the validator's master public key, then adds it to the `SHAMap` as `tnTRANSACTION_NM` (non-maleable, unsigned). These pseudo-transactions are not user-submitted and require no fee or signature; they exist purely as a ledger-state-modification mechanism agreed upon by all validating nodes running the same algorithm.

## Integration with RCLConsensus

`doVoting()` is invoked from `RCLConsensus::buildInitialSet()` specifically when `prevLedger->isVotingLedger()` is true, meaning the *next* ledger to close is a flag ledger and Negative UNL changes should be embedded. The `newValidators()` call happens separately in `RCLConsensus::startRound()` whenever the trusted validator set changes. Both paths share the same `NegativeUNLVote` instance (`nUnlVote_`) owned by `RCLConsensus`.

Two test classes are granted `friend` access — `NegativeUNLVoteInternal_test` and `NegativeUNLVoteScoreTable_test` — to exercise the private scoring and candidate-selection logic directly.