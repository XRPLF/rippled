# `CheckCash.cpp` — Check Redemption Transactor

## Role in the System

`CheckCash.cpp` implements the XRPL `CheckCash` transaction: the act of the designated recipient redeeming a check that was previously created via `CheckCreate`. The check mechanism is analogous to a paper bank check — the sender creates it with a maximum amount (`sfSendMax`) and a designated payee; only that payee can cash it, and they may ask for any amount up to the `sfSendMax`. This file contains the three-phase transactor lifecycle (`preflight` → `preclaim` → `doApply`) plus the feature-gating hook `checkExtraFeatures`.

`CheckCash` inherits from `Transactor` and declares `ConsequencesFactory{Normal}`, meaning it consumes the account's sequence number but does not require special fee escalation treatment.

## Feature Gating

`checkExtraFeatures()` exists to gate Multi-Purpose Token (MPT) usage behind the `featureMPTokensV2` amendment. The check fields `sfAmount` and `sfDeliverMin` can carry either a classic `Issue` or an `MPTIssue`. Without the amendment enabled, any `CheckCash` transaction specifying an MPT asset in either field is rejected before any further processing. This pattern — a static pre-screen run before `preflight` — lets the ledger cleanly introduce new transaction capabilities without forking validation logic across amendment conditions throughout the rest of the code.

## Preflight: Stateless Structural Validation

`preflight()` enforces three invariants without consulting ledger state:

1. **Mutual exclusivity**: Exactly one of `sfAmount` or `sfDeliverMin` must be present. `sfAmount` means "deliver exactly this amount or fail"; `sfDeliverMin` means "deliver as much as possible, but at least this much." The check `static_cast<bool>(optAmount) == static_cast<bool>(optDeliverMin)` cleanly handles both the "both absent" and "both present" error cases in one expression.

2. **Amount validity**: The unified value (whichever field is present) must pass `isLegalNet()` and be strictly positive.

3. **Asset validity**: The asset must not be the sentinel `badAsset()`.

## Preclaim: Stateful Contextual Validation

`preclaim()` consults the ledger view to validate the transaction against current state. It walks through a sequence of guards:

- The referenced check (`sfCheckID`) must exist on the ledger.
- Only the check's `sfDestination` may cash it (`tecNO_PERMISSION` otherwise). A self-check guard (`srcId == dstId`) is also present though marked `LCOV_EXCL_START` — it should be impossible if `CheckCreate` validated correctly, but is defended against as belt-and-suspenders.
- The destination account's `lsfRequireDestTag` flag is honored: if set, the check must have included `sfDestinationTag`.
- The check must not have expired.

Then the amount validation proceeds against the check's own `sfSendMax`. The request currency and issuer must match `sfSendMax` exactly. The requested value must not exceed `sfSendMax`. There is also a liquidity check against the source account's available funds (`accountFunds()` with `fhZERO_IF_FROZEN`). A notable subtlety here: when the amount is XRP, one reserve increment (`ctx.view.fees().increment`) is added to the reported available funds. This is because the check itself occupies one owner reserve slot; cashing it will free that slot, so the source effectively has one additional reserve's worth of XRP available to transfer.

For IOU assets where the destination is not the issuer itself, `preclaim()` validates the destination's eligibility to receive the asset. For classic IOUs this means verifying the issuer exists, checking `lsfRequireAuth` and the trust line's authorization flags using the canonical high/low account ordering, and checking that the destination's trust line is not frozen. For MPTs it uses `requireAuth()` with `AuthType::WeakAuth`, checks MPT-level freeze, and calls `canTrade()` to confirm the MPT is permitted on the DEX.

## `doApply()`: Execution

All mutations are staged in a `PaymentSandbox` (`psb`) wrapping the apply-context view. This is not optional — `flow()` is designed to operate on a `PaymentSandbox` because of its deferred-credit accounting, which prevents in-flight liquidity from being double-counted across path steps. Only at the very end does `psb.apply(ctx_.rawView())` commit changes; if anything fails partway, the sandbox is silently discarded.

### XRP Path

The payment engine's `flow()` does not handle native XRP transfers, so XRP cashing is handled directly. `xrpLiquid(psb, srcId, -1, viewJ)` computes how much XRP the source can actually move, passing `-1` as an owner-count adjustment to credit the reserve that will be freed when the check is deleted.

When `sfDeliverMin` is used, the actual delivery amount is `max(DeliverMin, min(sendMax, srcLiquid))`: deliver as much as possible up to the check's cap, so long as the minimum floor is met. For `sfAmount`, the requested amount is used verbatim. The transfer is executed via `transferXRP`.

### IOU/MPT Path

The IOU/MPT path delegates to `flow()` but requires careful setup:

**Trust line auto-creation.** Unlike a plain `Payment`, `CheckCash` automatically creates a trust line (for IOU) or `MPToken` entry (for MPT) if one does not yet exist. The logic reasoning is sound: the destination signed the `CheckCash` transaction, which is cryptographic proof they consent to receiving these funds. A reserve check via the `checkReserve` lambda confirms the destination can afford the new ledger entry before it is created. The trust line is created with zero balance and zero limit via `trustCreate()`; the transaction machinery will automatically clean it up if the subsequent payment fails.

**Trust line limit elevation.** Even with a trust line present, the destination may have set a limit that would be exceeded by the incoming funds. Since the destination explicitly consented, `doApply()` temporarily raises the trust line limit to `STAmount{cMaxValue, cMaxOffset}` — the maximum representable value — before calling `flow()`. A `scope_exit` guard restores the original limit unconditionally when the scope exits, regardless of whether `flow()` succeeded or failed. This is the central design choice of the IOU cashing path: RAII ensures the temporary limit mutation never escapes the function.

**DeliverMin capping.** When `sfDeliverMin` is specified, `flow()` is invoked with a target amount of `STAmount::cMaxValue / 2` (or `maxMPTokenAmount / 2` for MPTs) and `partial_payment = true`. The cap at half the maximum prevents overflow when gateway transfer rates (which can be up to 200%) are applied internally. After `flow()` returns, the `actualAmountOut` is compared to `*optDeliverMin`; if it falls short, `tecPATH_PARTIAL` is returned. In both `sfAmount` and `sfDeliverMin` cases, `ctx_.deliver(result.actualAmountOut)` records the delivered-amount metadata.

### Cleanup

After a successful transfer, `doApply()` removes the check from both the destination's and the source's owner directories, decrements the source's owner count (freeing the reserve), erases the check ledger entry, and finally calls `psb.apply()`. The directory removal failure paths are `LCOV_EXCL_START`-marked because a corrupt directory at this point would indicate a pre-existing ledger inconsistency, not a recoverable error.

## Relationship to Sibling Files

`CheckCancel.cpp` in the same directory follows the same structural pattern but is considerably simpler — it only removes the check from both directories and decrements the owner count, with no fund movement. `CheckCreate.cpp` establishes the check ledger entry that `CheckCash` and `CheckCancel` both operate on. The separation keeps each phase of the check lifecycle isolated and independently testable. The `flow()` function from `xrpl/tx/paths/Flow.h` carries the bulk of the IOU/MPT payment logic and is reused across `Payment`, `OfferCreate`, and check cashing, which is why `CheckCash` must wrap its mutations in a `PaymentSandbox`.