# `Change.cpp` — System Pseudo-Transaction Transactor

## Role in the System

`Change.cpp` implements the `Change` transactor, which processes three types of *pseudo-transactions* that XRPL validators inject into ledgers during consensus: amendment activations (`ttAMENDMENT`), fee schedule updates (`ttFEE`), and Negative UNL modifications (`ttUNL_MODIFY`). Pseudo-transactions are never submitted by users — they are constructed programmatically by the consensus machinery and embedded directly into ledger proposals. As a result, they carry a fundamentally different identity from normal transactions: zero account ID, zero fee, no signature, and sequence number zero.

The type aliases at the bottom of `Change.h` — `EnableAmendment`, `SetFee`, and `UNLModify` — all resolve to the same `Change` class, which is the idiomatic way the codebase documents which logical operation each transaction type performs without proliferating separate classes for closely related concerns.

## Validation Architecture

### Preflight: Template Specialization for Pseudo-Transactions

`invokePreflight<Change>` is a full template specialization of the generic `Transactor::invokePreflight`. The specialization exists because pseudo-transactions violate nearly every validation rule that applies to normal user transactions: they must have no cryptographic signature, no signing public key, no multisig signers list, a zero fee, a zero sequence, and a source account ID of `beast::zero`. Injecting these checks into a specialization lets the generic `invokePreflight` continue enforcing normal user-transaction rules without conditionals scattered throughout.

The flag validation in `invokePreflight` is notably pragmatic: flag mask enforcement via `tfEnableAmendmentMask` is only active when `featureLendingProtocol` is enabled. The inline comment explains this directly — adding a dedicated amendment purely to gate a flag mask check would be protocol complexity for no meaningful gain, so `featureLendingProtocol` serves as a proxy.

### Preclaim: Ledger-State and Transaction-Type Checks

`preclaim` rejects any `Change` transaction applied against an open ledger. Pseudo-transactions belong to closed ledgers only, and this guard enforces that boundary. The `ttFEE` case in `preclaim` also handles a field format transition driven by the `featureXRPFees` amendment. Before that feature is enabled, the fee object uses legacy integer fields (`sfBaseFee`, `sfReferenceFeeUnits`, `sfReserveBase`, `sfReserveIncrement`). After it is enabled, those are replaced by XRP-amount fields (`sfBaseFeeDrops`, `sfReserveBaseDrops`, `sfReserveIncrementDrops`). `preclaim` enforces that both sets of fields are mutually exclusive depending on which era the ledger is in, returning `temMALFORMED` or `temDISABLED` accordingly. This prevents a stale fee transaction format from silently mixing old and new fields after a feature upgrade.

## Amendment State Machine

`applyAmendment()` drives a three-state lifecycle recorded in the `sfAmendments` ledger object:

1. **Gaining majority** (`tfGotMajority`): The amendment's hash is recorded in `sfMajorities` alongside the current ledger close time. The close time is the anchor used to measure the two-week majority window enforced by the `AmendmentTable`. If the amendment is not supported by this server, a warning is logged but the change proceeds — the server remains functional at this stage.

2. **Losing majority** (`tfLostMajority`): The entry is removed from `sfMajorities`. The amendment returns to a "known but not voted in" state.

3. **Enabling** (no flags): The hash is moved from `sfMajorities` into the `sfAmendments` vector and `AmendmentTable::enable()` is called to update the in-memory feature table. If the enabled amendment is one this server does not implement, `NetworkOPs::setAmendmentBlocked()` is invoked, which puts the server into a degraded amendment-blocked state — the server stops validating and warns operators to upgrade.

The idempotency guards (`tefALREADY`) are carefully placed: a `tfGotMajority` on an amendment already in majority returns `tefALREADY`; a `tfLostMajority` on one that was never in majority returns `tefALREADY`; applying an amendment already in the enabled set returns `tefALREADY`. This prevents consensus replays from corrupting ledger state.

## Fee Schedule Updates

`applyFee()` overwrites the singleton `FeeSettings` ledger object (accessed via `keylet::fees()`). The implementation mirrors the field-era split from `preclaim`: under `featureXRPFees`, it writes the new `Drops`-suffixed fields and explicitly calls `makeFieldAbsent` to remove any legacy fields that might exist from earlier ledgers. This cleanup step ensures that the ledger object is canonical for the current era rather than carrying stale fields that could confuse fee-reading code.

## Negative UNL Modifications

`applyUNLModify()` implements staged changes to the Negative UNL (N-UNL), a feature allowing the network to continue reaching consensus even when a significant fraction of validators is offline. Modifications are recorded in the `NegativeUNL` ledger object and are only permitted on *flag ledgers* (every 256th ledger, enforced by `isFlagLedger`). This restriction matches the consensus protocol's amendment voting cycle.

The operation has two modes controlled by `sfUNLModifyDisabling`:

- **Disabling** (marking a validator for removal): recorded in `sfValidatorToDisable`. Only one validator can be staged for disabling at a time, the candidate must not already be in the negative UNL, and it cannot be the same validator currently staged for re-enabling.

- **Re-enabling** (returning a validator to the active UNL): recorded in `sfValidatorToReEnable`. The validator must already be present in `sfDisabledValidators`, and the same uniqueness/conflict checks apply symmetrically.

These "pending" fields (`sfValidatorToDisable`, `sfValidatorToReEnable`) function as a single-slot staging area: the transaction marks intent, and the full promotion/demotion into `sfDisabledValidators` is processed separately by ledger-closing logic. The asymmetric preconditions — disabling requires absence from N-UNL, re-enabling requires presence — enforce the logical invariant that you cannot remove what isn't there or restore what was never disabled. All validation failures in `applyUNLModify` return `tefFAILURE`, indicating server-level rejection without network-level blame.

## Design Note: `preCompute`

`preCompute()` asserts that `account_` is `beast::zero`. This is a defensive invariant check that fires during the apply phase to confirm the pseudo-transaction's source account was never overwritten after `preflight` validated it. It catches any future refactor that might accidentally hydrate a real account ID into a `Change` transactor context.