# Escrow.cpp — Module Placeholder for the XRPL Escrow Transactors

`Escrow.cpp` is an empty file — it contains zero bytes. Its significance is structural rather than functional: it nominally anchors the escrow transactor module within the build system and signals the conceptual boundary of a feature whose implementation is entirely distributed across three sibling files in the same directory.

## Module Context

The escrow subsystem spans four files: this empty placeholder and the three active implementation units `EscrowCreate.cpp`, `EscrowFinish.cpp`, and `EscrowCancel.cpp`. Together they implement XRPL's conditional payment mechanism, which locks XRP or tokens into a ledger object that can only be released to a destination under specific conditions — a time-based unlock, a cryptographic condition/fulfillment pair, or both. The canonical module-level comment describing all three transaction types lives in `EscrowCreate.cpp` rather than here, which reinforces that this file is vestigial.

The empty file is most likely a build system artifact: either CMake required it when the escrow logic was originally consolidated in one file, or a refactor split a formerly monolithic `Escrow.cpp` into three specialized transactors without removing the now-empty source.

## The Three Transactors

**`EscrowCreate`** locks funds and writes a new `SLE` (Serialized Ledger Entry) into the ledger. The keylet is derived from the creator's account and their current sequence value (`keylet::escrow(account_, ctx_.tx.getSeqValue())`), making each escrow globally addressable with no additional state. The `doApply()` phase inserts the escrow into up to three owner directories: the sender's, the recipient's (when different from the sender), and — for IOU escrows only, not MPT — the issuer's. The issuer directory entry exists so the issuer can track total locked balance against their outstanding obligations. MPT does not need this because locked balance is tracked directly in the `MPTokenIssuance` object.

A non-obvious design choice is the transfer rate snapshot: when `featureTokenEscrow` is active and the amount carries a non-parity transfer rate, the current rate is stored in the escrow SLE as `sfTransferRate`. This is a deliberate protection against issuer rate manipulation — the rate is frozen at creation and compared against the current rate at finish time; whichever is lower wins. Without this mechanism, an issuer could raise their transfer fee after an escrow is created, retroactively penalizing the escrow recipient.

The `preflight` function enforces a subtle invariant: at least one of `sfFinishAfter` or `sfCondition` must be present. Without either, an escrow could theoretically be finished immediately and unconditionally — legal but confusing enough to be disallowed.

**`EscrowFinish`** is the most complex of the three. It carries the burden of cryptographic condition verification, time window enforcement, and — for token escrows — potential trust line or MPToken account creation on delivery. The fulfillment size feeds directly into `calculateBaseFee()`: the fee surcharge is `base * (32 + size / 16)`, penalizing large fulfillments to prevent computational DoS attacks.

Crypto-condition validation (`checkCondition()`) is expensive to run, and a single transaction can be replayed multiple times during consensus processing. To avoid redundant work, the result is cached in the `HashRouter` using two private flag bits (`SF_CF_INVALID` / `SF_CF_VALID`). The `preflightSigValidated()` phase populates these bits; `doApply()` reads them and falls back to recomputing only if the flags have aged out of the router. The escrow SLE's own `sfCondition` field is then compared byte-for-byte against the transaction's `sfCondition` to prevent finish attempts that supply a valid but different condition than the one locked at creation.

For IOU token escrows, `doApply()` delegates to `escrowUnlockApplyHelper<Issue>` (defined in `EscrowHelpers.h`), which may create a trust line for the receiver on the fly if one doesn't exist — provided the receiver has enough XRP reserve to cover it. This mirrors behavior in ordinary payments but must be handled explicitly here because escrow finish is not routed through the payment path.

**`EscrowCancel`** is intentionally minimal. Its `preflight` unconditionally returns `tesSUCCESS` — all meaningful checking happens in `doApply()` against live ledger state. The key guard is time: cancellation is only permitted after `sfCancelAfter` has passed. Escrows with no cancel time cannot be cancelled at all. On cancel, tokens are returned at `parityRate` (no transfer fee), because the funds are simply being restored to their origin rather than being transferred to a third party.

## Validation Architecture

All three transactors follow XRPL's three-phase transaction pipeline: `preflight` (static, no ledger access) → `preclaim` (read-only ledger checks) → `doApply` (mutating). Field-level validation — empty strings, type checks, format validity — is handled automatically by `STObject` field deserialization and the `amountFromValue` / `accountFromStringStrict` helpers, so manual null checks do not appear in the business logic code. Cryptographic condition format is validated by deserializing with `Condition::deserialize()` in preflight and failing with `temMALFORMED` on parse error.

For token escrows, `preclaim` in both `EscrowCreate` and `EscrowCancel` dispatches via `std::visit` over `amount.asset().value()`, branching on `Issue` vs `MPTIssue` at compile time through the `ValidIssueType` concept. This pattern avoids a runtime `if`/`else` on asset type and ensures that new asset types added in the future will produce a compile-time error rather than a silent skip.