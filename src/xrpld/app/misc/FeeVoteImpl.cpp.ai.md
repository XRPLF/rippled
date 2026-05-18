# `FeeVoteImpl.cpp` — Validator Fee Voting Logic

## Role in the System

XRPL's base fees and reserve requirements are not fixed in source code — they can be changed through a decentralized on-ledger voting process. `FeeVoteImpl.cpp` is the sole implementation of this mechanism. Each validating node embeds its fee preferences into every validation it broadcasts, and at every 256th ("flag") ledger, those preferences are tallied to produce a pseudo-transaction that may update the network's fee schedule.

The file defines two collaborating types: `detail::VotableValue` (the vote-counting primitive, private to this translation unit) and `FeeVoteImpl` (the `FeeVote` interface implementation exposed via the `make_FeeVote()` factory). The abstract `FeeVote` interface in `FeeVote.h` forward-declares `FeeSetup` and exposes only `doValidation()` and `doVoting()`, keeping all voting logic behind the interface and out of headers.

## `VotableValue`: Safe Incremental Change

The design of `VotableValue` encodes a deliberate philosophy: fee changes should be gradual and consensus-driven, never a large step imposed by a minority. The class holds the ledger's *current* fee value and the node operator's *target* preference. Its vote map accumulates one tally per distinct `XRPAmount` value proposed across all trusted validators.

The key safety property is enforced in `getVotes()`:

```cpp
if ((key <= std::max(target_, current_)) && (key >= std::min(target_, current_)) && (val > weight))
```

Only votes that fall strictly within the range `[min(current, target), max(current, target)]` are considered. A validator trying to push a fee far beyond any locally-configured target has its vote silently discarded. This bounding prevents a small group of adversarial or misconfigured validators from swinging fees by an arbitrary amount in one ledger cycle — movement is always capped by the distribution of operator preferences.

The constructor immediately registers the local node's own vote (`++voteMap_[target_]`), so the local preference is counted even before external validations are processed.

Validators that don't include a fee field in their validation are handled by `noVote()`, which calls `addVote(current_)`. Abstaining is treated as a vote for the status quo, meaning changes require active positive consensus rather than passive majority.

## Two-Phase Voting: Validation vs. Consensus

Fee voting is split across two consensus phases, both routed through `RCLConsensus.cpp`.

**Phase 1 — `doValidation()`** runs when a validator is about to sign and broadcast a validation message for a just-closed ledger. If the node's target fee differs from the ledger's current fee, it encodes its preferred value in the `STValidation` object. This field acts as the node's public ballot: every peer that receives this validation can observe the preference.

**Phase 2 — `doVoting()`** runs only at flag ledgers (every 256 ledgers), gated by `XRPL_ASSERT(isFlagLedger(...))`. It receives all trusted parent validations and tallies the votes for each of the three fee parameters (base fee, base reserve, reserve increment) independently. If any of the three tallied outcomes differs from the current ledger value, the method constructs a `ttFEE` pseudo-transaction and inserts it into the `SHAMap` representing the node's initial consensus proposal. This pseudo-transaction carries the winning vote values, not the node's own preference — the outcome is whatever achieved plurality within the safe range.

The guard in `RCLConsensus.cpp` adds an extra condition: `doVoting()` is only called if the number of available trusted validations meets quorum. This prevents a fee change from being proposed based on an unrepresentative sample during network partitions.

## `featureXRPFees` Amendment Dual Code Path

Both `doValidation()` and `doVoting()` maintain two parallel code branches controlled by `rules.enabled(featureXRPFees)`. Before the amendment activated, fees in validations and pseudo-transactions were encoded as legacy integer fields: `sfBaseFee` (uint64), `sfReserveBase` (uint32), `sfReserveIncrement` (uint32), along with the deprecated `sfReferenceFeeUnits`. After the amendment, they become `sfBaseFeeDrops`, `sfReserveBaseDrops`, and `sfReserveIncrementDrops`, all typed as `SF_AMOUNT` (native XRP).

This dual path is necessary for a live network upgrade: during the transition window, the codebase must be able to read and write both formats. The legacy branch includes an explicit `dropsAs<>()` narrowing conversion with a fallback default — if the `XRPAmount` doesn't fit in a 32-bit value, the current ledger value is used rather than producing a truncated garbage value.

In the legacy vote-parsing path inside `doVoting()`, external values arriving as raw integers are validated against both the `numeric_limits` of the underlying `XRPType` and `isLegalAmountSigned()`. Out-of-range votes call `noVote()` rather than throwing — the comment explicitly notes "Don't throw because this value is provided by an external entity," a defensive coding decision that isolates the node from malformed peer input.

## Pseudo-Transaction Construction

When `doVoting()` determines at least one fee parameter should change, it builds an `STTx` with transaction type `ttFEE`. This is a synthetic ledger-management transaction, not a user-submitted transaction: its `sfAccount` is the zero `AccountID` and it carries a `sfLedgerSequence` equal to `lastClosedLedger->seq() + 1`. The transaction is serialized and inserted into the SHAMap via `addGiveItem()`. If the SHAMap already contains an equivalent fee pseudo-transaction (possible if two validators propose identical changes and their proposals are merged), `addGiveItem()` returns false and the duplicate is logged at warning level.

## Factory and Ownership

`make_FeeVote()` is the only public entry point for obtaining a `FeeVoteImpl`. It returns `std::unique_ptr<FeeVote>`, enforcing that callers own the object exclusively and cannot observe the concrete type. The `FeeSetup` struct (defined in `Config.h`) defaults to 10 drops base fee, 10 XRP base reserve, and 2 XRP owner reserve — these are the protocol's conservative recommended defaults. Operator configuration overrides are applied at construction time and stored immutably in `target_`.