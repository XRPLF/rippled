# `CheckCreate.h` — Check Creation Transactor Interface

## Role in the System

`CheckCreate.h` declares the transactor responsible for handling `CheckCreate` transactions on the XRP Ledger. A Check is a deferred payment authorization: the sender (drawer) commits a `SendMax` amount to a named destination, which the destination may later cash via `CheckCash`, or which either party may cancel via `CheckCancel`. This file defines the interface of the `CheckCreate` class, while the full validation and ledger-mutation logic lives in `CheckCreate.cpp`.

It is one of three sibling transactors in the `check/` subdirectory alongside `CheckCash` and `CheckCancel`, all of which share an identical structural interface.

## The Transactor Framework

`CheckCreate` inherits from `Transactor`, the common base for all XRPL transaction processors. The framework enforces a strict three-phase execution model: **preflight** (stateless validation, before any ledger read), **preclaim** (ledger-state validation, read-only), and **doApply** (ledger mutation). Each phase maps directly to a method in this class.

A critical design detail: `preflight` and `preclaim` are **static** methods, not virtual. The base class `Transactor` invokes them through the `invokePreflight<T>` template, which uses name hiding for compile-time polymorphism. The comment in `Transactor.h` is explicit — derived classes must not define `invokePreflight` themselves, and must not call `preflight1` or `preflight2` directly. This pattern trades the safety of virtual dispatch for zero-overhead static dispatch, which matters given how frequently these validation paths run.

Only `doApply()` is virtual, reflecting the fact that ledger mutation requires runtime dispatch while validation does not.

## `ConsequencesFactory`

The class exposes `static constexpr ConsequencesFactoryType ConsequencesFactory{Normal}`. This constant tells the framework how to classify the transaction's consequences — specifically, whether it blocks or merely consumes a sequence number. `Normal` means `CheckCreate` does not block subsequent transactions from the same account, unlike some transactors (e.g., escrow-related ones) that use `Blocker`. This affects how the transaction queue reasons about batching and ordering.

## `checkExtraFeatures`

The static `checkExtraFeatures(PreflightContext const&)` hook is called from `invokePreflight` before `preflight1` runs. The base `Transactor` implementation always returns `true`; `CheckCreate` overrides it to return `temDISABLED` when the transaction uses an `MPTIssue` (Multi-Purpose Token) as `SendMax` but the `featureMPTokensV2` amendment is not yet enabled. This keeps the feature-gating concern separate from the core field-validation logic in `preflight`, which is the intended pattern described in `Transactor.h`.

## Validation Phases

`preflight` performs pure field-level checks that require no ledger access: it rejects self-addressed checks (`temREDUNDANT`), validates that `SendMax` is a positive, legally-formed amount with a non-bad currency (`temBAD_AMOUNT`, `temBAD_CURRENCY`), and ensures any `Expiration` field is non-zero (`temBAD_EXPIRATION`).

`preclaim` handles ledger-dependent validation. It verifies the destination account exists (`tecNO_DST`), checks that the destination has not set `lsfDisallowIncomingCheck`, and enforces that pseudo-accounts cannot receive checks. It also enforces `lsfRequireDestTag` on the destination. For non-XRP amounts, it checks global and per-trustline freeze state, distinguishing between `tecFROZEN` (IOU) and `tecLOCKED` (MPT). Notably, it permits creating a check for a currency even if the sender has no existing trustline — the check is speculative and the trustline need not exist at creation time. Finally, it rejects checks with an expiration already in the past (`tecEXPIRED`), and validates that the asset is tradeable via `canTrade`.

## `doApply` Ledger Mutation

`doApply()` constructs the `Check` SLE (Serialized Ledger Entry), keyed by `keylet::check(account_, seq)` where `seq` is the transaction's sequence or ticket value. It populates all optional fields (`SourceTag`, `DestinationTag`, `InvoiceID`, `Expiration`) only if present in the transaction, leaving them absent otherwise. The Check is inserted into **both** the sender's owner directory and the destination's owner directory, with `sfOwnerNode` and `sfDestinationNode` recording each directory page. Reserve sufficiency is checked against `preFeeBalance_` (the pre-fee balance set by the base class) rather than the post-fee balance, intentionally allowing fee payment to dip into the reserve while still requiring reserve coverage for the new ledger object. On success, `adjustOwnerCount` increments the sender's owner count, increasing their reserve requirement by one increment.

## Relationship to Sibling Transactors

`CheckCash` and `CheckCancel` have structurally identical declarations. Together they form a complete lifecycle: `CheckCreate` establishes the Check object on the ledger, `CheckCash` redeems it (consuming the object), and `CheckCancel` removes it without payment. All three use `ConsequencesFactory{Normal}` and override only `checkExtraFeatures`, `preflight`, `preclaim`, and `doApply` — the minimal surface dictated by the framework.