# Consensus

Template-based state machine in `Consensus.h` parameterized by an Adaptor (`RCLConsensus`). Three phases: open -> establish -> accepted. Modes: proposing, observing, wrongLedger, switchedLedger.

## Key Invariants

- A ledger cannot close until the previous ledger reaches consensus AND (has transactions OR close time reached)
- Proposals must have strictly increasing sequence numbers per peer; stale proposals are silently dropped
- The Avalanche state machine progressively lowers consensus thresholds over time (init -> mid -> late -> stuck) to prevent livelock
- `minCONSENSUS_PCT = 80` is the baseline; timing params: `ledgerMIN_CONSENSUS = 1950ms`, `ledgerMAX_CONSENSUS = 15s`
- Dead nodes (`deadNodes_`) are permanently excluded for the round once they bow out

## Common Bug Patterns

- Proposals referencing a stale `prevLedgerID_` after a ledger switch cause split-brain; always check `newPeerProp.prevLedger() != prevLedgerID_` before processing
- Resetting the consensus timer during `establish` phase causes re-convergence and potential split; timer must only reset on phase transitions
- `DisputedTx::updateVote` changes local vote based on peer pressure; bugs here cause determinism failures across nodes
- `createDisputes()` deduplicates via `compares` set; missing this check creates duplicate disputes that skew vote counts
- The `peerUnchangedCounter_` is reset to 0 when any vote changes; bugs in this counter cause premature consensus declaration

## Amendments

- 80% validator support for 2 weeks to enable; tracked via `AmendmentTable` with `amendmentMap_`
- New amendments: add to `features.macro` with `XRPL_FEATURE`/`XRPL_FIX`, increment `numFeatures` in `Feature.h`
- Unsupported enabled amendment blocks the server (`setAmendmentBlocked`); no mechanism to disable/revoke
- Voting happens each consensus round in `doVoting`; votes are persisted in `FeatureVotes` SQLite table
- `fixAmendmentMajorityCalc` changed the threshold calculation; check which calculation applies

## UNL and Negative UNL

- Negative UNL temporarily disables unreliable validators (max 25% of UNL: `negativeUNLMaxListed = 0.25`)
- Scoring uses `buildScoreTable` over recent ledger history; low watermark (50%) = disable candidate, high watermark (80%) = re-enable candidate
- Candidate selection is deterministic via previous ledger hash as randomizing pad
- `newValidatorDisableSkip = FLAG_LEDGER_INTERVAL * 2` prevents disabling newly joined validators prematurely

## Validations

- `ValidationParms` defines freshness windows: CURRENT_WALL=5min, CURRENT_LOCAL=3min, SET_EXPIRES=10min, FRESHNESS=20s
- `SeqEnforcer` rejects validations with regressed or duplicate sequence numbers (`ValStatus::badSeq`)
- Conflicting validations (same seq, different hash) are logged as byzantine behavior
- `handleNewValidation` is the entry point: checks trust, adds to `Validations` set, triggers `checkAccept` if current+trusted

## Transaction Ordering

- `CanonicalTXSet` orders by: salted account key (XOR with random salt) -> sequence proxy -> transaction ID
- Salt prevents manipulation of ordering by account selection
- `TxQ` uses `OrderCandidates`: higher fee level first, then `txID XOR parentHash` as tiebreaker
- Per-account limit: `maximumTxnPerAccount`; blocked transactions held until blocker resolves

## Key Patterns

### Proposal Validation (prevents split-brain)
```cpp
// REQUIRED: reject proposals referencing stale previous ledger
if (newPeerProp.prevLedger() != prevLedgerID_)
{
    JLOG(j_.debug()) << "Got proposal for " << newPeerProp.prevLedger()
                     << " but we are on " << prevLedgerID_;
    return;
}
```

### Complete Bow-Out Handling
```cpp
// REQUIRED: all three steps — unvote, erase position, mark dead
if (newPeerProp.isBowOut())
{
    if (result_)
        for (auto& it : result_->disputes)
            it.second.unVote(peerID);
    if (currPeerPositions_.find(peerID) != currPeerPositions_.end())
        currPeerPositions_.erase(peerID);
    deadNodes_.insert(peerID);  // permanently excluded this round
}
```

## Key Files

- `src/xrpld/consensus/Consensus.h` - state machine
- `src/xrpld/consensus/ConsensusParms.h` - timing/threshold params
- `src/xrpld/app/consensus/RCLConsensus.cpp` - XRPL adaptor
- `src/xrpld/consensus/DisputedTx.h` - dispute tracking
- `src/xrpld/app/misc/detail/AmendmentTable.cpp` - amendment logic
- `src/xrpld/app/misc/NegativeUNLVote.cpp` - N-UNL voting
- `src/xrpld/consensus/Validations.h` - validation tracking
- `src/xrpld/app/misc/CanonicalTXSet.h` - TX ordering
