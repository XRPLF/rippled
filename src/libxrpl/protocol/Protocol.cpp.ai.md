# `src/libxrpl/protocol/Protocol.cpp`

## Role and Purpose

This file provides the two predicate functions that the rest of the XRPL node uses to identify **flag ledgers** and **voting ledgers** — the two special milestone points built into the ledger sequence at regular, protocol-defined intervals. Although the file is only 15 lines of code, the names it exposes anchor a surprisingly wide swath of consensus, fee governance, and validator-reliability logic throughout the codebase.

## The `FLAG_LEDGER_INTERVAL` Heartbeat

The key constant lives in the companion header, `Protocol.h`:

```cpp
std::uint32_t constexpr FLAG_LEDGER_INTERVAL = 256;
```

Every 256 ledgers, the network reaches a boundary where accumulated validator votes are tallied and network-wide parameters are updated. This boundary is the **flag ledger**. Changing `FLAG_LEDGER_INTERVAL` without an amendment mechanism would be a hard fork — it is explicitly called out in `Protocol.h`'s comment block as an implicit part of the protocol.

## The Two Predicates

Both functions share an identical body:

```cpp
bool isVotingLedger(LedgerIndex seq) { return seq % FLAG_LEDGER_INTERVAL == 0; }
bool isFlagLedger(LedgerIndex seq)   { return seq % FLAG_LEDGER_INTERVAL == 0; }
```

Their names are intentionally distinct despite the identical implementation, because the **caller** provides different `seq` values for each purpose. The `Ledger` class illustrates this clearly:

```cpp
bool Ledger::isFlagLedger()  const { return ::xrpl::isFlagLedger(header_.seq); }
bool Ledger::isVotingLedger() const { return ::xrpl::isVotingLedger(header_.seq + 1); }
```

`isFlagLedger()` asks "is *this* ledger a flag ledger?" — it passes the ledger's own sequence number directly. `isVotingLedger()` asks "does *this* ledger's consensus session produce a flag ledger?" — it passes `seq + 1`, so a ledger is a voting ledger when the ledger being built on top of it will land on a flag boundary. This `+1` offset is the entire semantic difference between the two names, and it is resolved at the call site rather than inside the protocol functions.

## How the Distinction Drives Consensus

`RCLConsensus.cpp` uses both predicates in sequence when assembling the transaction set for a new consensus round:

- If the **previous ledger** was a flag ledger, fee-vote and amendment pseudo-transactions are injected via `feeVote_->doVoting()` and `app_.getAmendmentTable().doVoting()`. These pseudo-transactions encode the outcome of the vote that was collected in the flag ledger's validation messages.
- If the **previous ledger** was a voting ledger (i.e., the session is building the flag ledger itself), Negative UNL pseudo-transactions are added via `nUnlVote_.doVoting()`. The Negative UNL update records which validators were unreliable over the preceding 256-ledger window.

`FeeVoteImpl.cpp` independently gates its vote-casting on `isFlagLedger(lastClosedLedger->seq())`, and `Change.cpp` guards the application of fee/reserve updates behind the same check. This means all three subsystems — fee governance, amendment governance, and validator-reliability tracking — synchronize on the same 256-ledger clock tick defined here.

## Why Two Names for the Same Check

Separating `isFlagLedger` and `isVotingLedger` serves as **self-documenting intent**. A reader seeing `prevLedger->isFlagLedger()` immediately understands the context is "we just crossed a boundary, apply stored votes." A reader seeing `prevLedger->isVotingLedger()` understands "we are *about to* produce a flag ledger, inject the vote now." If the two calls used the same name, the `+1` offset in `Ledger::isVotingLedger()` would be invisible at the consensus call sites, making the phase relationship between vote collection and vote application harder to audit.

The `NegativeUNLVote` subsystem extends this pattern further, deriving threshold constants directly from `FLAG_LEDGER_INTERVAL` (e.g., 50%, 80%, and 90% of 256 for reliability scoring watermarks), reinforcing that the 256-ledger period is the canonical unit of measurement for network health metrics.