# `CheckCreate.cpp` — Check Ledger Object Creation

## Role in the System

`CheckCreate.cpp` implements the first leg of the XRPL check lifecycle: writing a `Check` ledger object that authorizes a designated recipient to pull funds from the sender's account at a later time. The analogy to a paper bank check is intentional — the sender (`sfAccount`) specifies a maximum drawable amount (`sfSendMax`) denominated in XRP, an IOU, or (with the `featureMPTokensV2` amendment) an MPT. The named destination can cash it via `CheckCash` for any amount up to that cap, or either party can destroy it via `CheckCancel`. This file contains all three phases of the XRPL transactor lifecycle for that creation step.

The `CheckCreate` class inherits from `Transactor` and declares `ConsequencesFactory{Normal}`, meaning it consumes the sender's sequence number under the standard fee model with no special escalation.

## Feature Gating

`checkExtraFeatures()` is a static pre-screen that runs before `preflight`. Its single responsibility is to block MPT-denominated checks unless the `featureMPTokensV2` amendment is active. The expression `ctx.rules.enabled(featureMPTokensV2) || !ctx.tx[sfSendMax].holds<MPTIssue>()` is a short-circuit gate: if the amendment is enabled, any asset is allowed through; if not, an `MPTIssue` in `sfSendMax` causes immediate rejection. This pattern isolates feature-flag logic at the boundary, keeping the rest of the validation code free of amendment conditions.

## `preflight`: Stateless Structural Validation

`preflight()` validates the transaction fields without consulting ledger state. Three invariants are enforced:

1. **Self-check rejection.** If `sfAccount == sfDestination`, the transaction returns `temREDUNDANT`. A check written to oneself has no economic purpose and the check mechanism cannot serve it.

2. **`sfSendMax` integrity.** The amount must pass `isLegalNet()` (confirming it is representable on the network) and `signum() > 0` (no zero or negative amounts). Additionally, the asset must not equal `badAsset()`, guarding against a malformed currency code. These are `tem`-class errors because they reflect malformed transactions that should never have been submitted.

3. **Expiration sanity.** `sfExpiration` is optional, but if present it must not be zero. An expiration of zero would immediately make the check unspendable, which is almost certainly a client error.

## `preclaim`: Stateful Contextual Validation

`preclaim()` consults the current ledger state to determine if creating the check is permissible. Validations proceed in a deliberate order, each guarded by an early `return`:

**Destination account existence and permissions.** The destination account must exist (`tecNO_DST` if not). Its account flags are then checked: `lsfDisallowIncomingCheck` lets an account opt out of receiving checks entirely, returning `tecNO_PERMISSION`. Pseudo-accounts are also blocked with `tecNO_PERMISSION` via `isPseudoAccount(sleDst)`. The comment explains why this check is not amendment-gated: the discriminator fields that mark an account as a pseudo-account are themselves behind amendments, so the behavior automatically tracks whatever amendments are active.

**Destination tag requirement.** If the destination has `lsfRequireDestTag` set, the transaction must include `sfDestinationTag`. This is a common convention for hosted wallets that use the tag field to route funds internally — `tecDST_TAG_NEEDED` tells the sender to retry with the tag.

**Freeze checks for non-native assets.** When `sfSendMax` is not XRP, the code distinguishes between IOU (`Issue`) and MPT (`MPTIssue`) assets using a `visit` lambda dispatch. For IOUs, global freeze is checked first (`tecFROZEN`), then the individual trustline between the sender and the issuer, and finally the trustline between the issuer and the destination. The high/low account ordering used by `lsfHighFreeze`/`lsfLowFreeze` flags is respected explicitly. For MPTs, the parallel `isFrozen()` helper checks individual account-level freeze, returning `tecLOCKED`. Both branches only validate sender and destination when they are not the issuer themselves — the issuer cannot freeze their own side of a line for self-directed operations.

**Pre-creation expiry.** `hasExpired()` ensures the check would not be immediately expired upon creation, returning `tecEXPIRED`. This prevents creating dead entries that consume reserve with no utility.

**Trade capability.** The final call to `canTrade(ctx.view, ctx.tx[sfSendMax].asset())` confirms the asset supports trading at all — a catch-all relevant primarily to MPTs that may have trading disabled at the MPT level.

## `doApply`: Ledger Mutation

`doApply()` runs after both validation phases have passed and makes the following mutations atomically:

**Reserve enforcement.** The check uses `preFeeBalance_` (the sender's balance before the transaction fee was deducted) against the reserve requirement for `ownerCount + 1`. Using the pre-fee balance is deliberate — it allows an account that is near the reserve floor to still pay the fee and create the check, rather than being locked out of transacting entirely.

**SLE construction.** The `Check` ledger entry is keyed by `keylet::check(account_, seq)` where `seq` is `ctx_.tx.getSeqValue()`. Using the transaction's sequence (or ticket) value as the check's ledger key is not accidental: it makes the key deterministically computable from information the recipient already knows, without requiring a ledger lookup. All required fields (`sfAccount`, `sfDestination`, `sfSequence`, `sfSendMax`) are written unconditionally. The four optional fields — `sfSourceTag`, `sfDestinationTag`, `sfInvoiceID`, and `sfExpiration` — are written only if present in the transaction, using the `ctx_.tx[~sfField]` optional accessor pattern.

**Dual directory insertion.** The check is inserted into both the destination's owner directory and the source's owner directory. The resulting page numbers are stored back onto the SLE as `sfDestinationNode` and `sfOwnerNode`. This is the standard XRPL pattern for objects that need to be removed during later operations: `CheckCash` and `CheckCancel` both use these stored page numbers to call `dirRemove()` in O(1) without traversing the directory tree. The `tecDIR_FULL` paths after each insertion are marked `LCOV_EXCL_LINE` because a full owner directory represents a pre-existing ledger anomaly rather than an expected validation failure.

**Owner count adjustment.** `adjustOwnerCount(view(), sle, 1, viewJ)` increments the sender's owner count, which increases the reserve requirement for future transactions. This links back to the `doApply()` reserve check: the sender must have had enough headroom before the fee was applied to absorb this increment.

## Relationship to Sibling Files

`CheckCancel.cpp` and `CheckCash.cpp` in the same directory both operate on the `Check` ledger entry that `CheckCreate.cpp` produces. `CheckCancel` is the simplest of the three — it merely removes the entry from both directories and decrements the owner count. `CheckCash` is the most complex, involving the payment engine, trust line manipulation, and `PaymentSandbox`. The structural data written by `doApply()` — particularly `sfOwnerNode`, `sfDestinationNode`, and the check keylet — is what enables those sibling transactors to locate and clean up the entry without additional state.