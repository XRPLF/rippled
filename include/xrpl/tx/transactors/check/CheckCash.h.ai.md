# `CheckCash.h` — Transactor Declaration for the CheckCash Transaction

## Purpose and Context

`CheckCash.h` declares the `CheckCash` class, which is the transactor responsible for executing `ttCHECK_CASH` (transaction type 17) on the XRP Ledger. It lives alongside `CheckCreate.h` and `CheckCancel.h` as one of three transactors that together implement the XRPL Checks feature — an asynchronous, pull-based payment mechanism where a sender authorizes a recipient to withdraw up to a specified amount at a future time of the recipient's choosing.

The header itself is minimal by design: all three check transactors follow the same structural pattern, declaring only the pipeline hooks required by the `Transactor` framework. The real complexity lives in the corresponding `.cpp` file.

## Class Structure

```cpp
class CheckCash : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};
    explicit CheckCash(ApplyContext& ctx) : Transactor(ctx) {}

    static bool     checkExtraFeatures(PreflightContext const& ctx);
    static NotTEC   preflight(PreflightContext const& ctx);
    static TER      preclaim(PreclaimContext const& ctx);
    TER             doApply() override;
};
```

`CheckCash` inherits from `Transactor` and participates in the framework's compile-time polymorphism pipeline. The four methods correspond to four distinct execution phases; they are invoked through `invokePreflight<T>` and `invoke_preclaim<T>` template machinery using name hiding rather than virtual dispatch, which allows the framework to combine standard checks (signature verification, fee validation, sequence numbers) with transaction-specific logic without virtual call overhead at each step.

## `ConsequencesFactory{Normal}`

The `Normal` factory type means that a `CheckCash` transaction claims its fee on failure just as it would on success. This is the default posture for most transactors. The alternative, `Blocker`, would mark the transaction as preventing other transactions from the same account from processing — not appropriate for check cashing, which should fail cleanly without affecting the sender's queue.

## `checkExtraFeatures` — Amendment-Gated Feature Detection

`CheckCash` is one of the few transactors that overrides `checkExtraFeatures` (the base class default simply returns `true`). The override checks whether the transaction attempts to cash a check denominated in an MPT (Multi-Purpose Token) asset, and if so, requires the `featureMPTokensV2` amendment to be enabled:

```cpp
return ctx.rules.enabled(featureMPTokensV2) ||
    (!(optAmount && optAmount->holds<MPTIssue>()) &&
     !(optDeliverMin && optDeliverMin->holds<MPTIssue>()));
```

The `Transactor` framework explicitly separates amendment checks from the `preflight` body: the comment in `Transactor.h` states "Do not check whether relevant amendments are enabled in preflight. Instead, define `checkExtraFeatures`." This separation keeps `preflight` focused on structural transaction validity and localizes all amendment gating to a single discoverable hook.

Note that `CheckCancel` does not override `checkExtraFeatures`, which indicates cancellation of MPT checks does not require an additional amendment gate — only cashing them does.

## `preflight` — Stateless Structural Validation

Preflight runs before the transaction is applied to any ledger view and serves as the first line of defense. For `CheckCash`, it enforces two rules:

1. **Mutual exclusivity of `sfAmount` and `sfDeliverMin`**: Exactly one must be present. `sfAmount` means "deliver exactly this amount"; `sfDeliverMin` means "deliver at least this much, up to the check's `sfSendMax`." Both present or neither present is `temMALFORMED`.

2. **Amount validity**: The chosen amount must pass `isLegalNet()` and be strictly positive. The asset must not be a `badAsset()`.

These checks are intentionally limited to data the transaction itself carries — no ledger access occurs here.

## `preclaim` — Read-Only State Validation

Preclaim receives a `ReadView` and runs after preflight succeeds. It performs a richer set of validations without modifying ledger state:

- Verifies the check ledger entry (`keylet::check(sfCheckID)`) exists — `tecNO_ENTRY` otherwise.
- Confirms that the submitting account is the check's destination — `tecNO_PERMISSION` otherwise.
- Detects the degenerate self-send case (source equals destination) that should have been caught during `CheckCreate` — returns `tecINTERNAL` under `LCOV_EXCL` guards, signaling this is a "should never happen" path.
- Enforces the destination's `lsfRequireDestTag` flag if the check lacks a `sfDestinationTag`.
- Rejects expired checks via `hasExpired()`.
- Cross-checks the requested amount's asset and issuer against the check's `sfSendMax`, and ensures the requested amount does not exceed `sfSendMax`.
- Confirms the source has sufficient available funds. Notably, for XRP checks, it adds one reserve increment to the available balance — a forward-looking adjustment because cashing the check will delete the check ledger entry, releasing that reserve back to the source before the transfer is calculated.
- For IOU assets not self-issued by the destination, validates that the issuer exists, that trust line authorization is satisfied (`lsfRequireAuth` / `lsfLowAuth` / `lsfHighAuth`), and that the destination's trust line to the issuer is not frozen.
- For MPT assets, checks `requireAuth` with weak authorization and `isFrozen()` on the destination's MPT holding, plus `canTrade()` to confirm the asset's DEX trading is permitted.

## `doApply` — Ledger Mutation

`doApply` is the only virtual method and the only one that actually modifies state. It operates on a `PaymentSandbox` — a copy-on-write wrapper around the current `ApplyView` — and calls `psb.apply(ctx_.rawView())` only at the very end after all mutations succeed. This ensures atomicity: if anything fails mid-execution, the sandbox is simply discarded.

**XRP path**: `flow()` does not handle XRP-to-XRP transfers, so `CheckCash` handles them directly. It calls `xrpLiquid(psb, srcId, -1, viewJ)` with the `-1` argument to account for the reserve that will be freed once the check is deleted, then computes the delivery amount. For `sfDeliverMin`, the delivery is `max(DeliverMin, min(sendMax, srcLiquid))` — the maximum the source can actually send, bounded by both the check cap and their available liquidity. `ctx_.deliver()` is called to set the `DeliveredAmount` metadata field.

**IOU/MPT path**: The `flow()` payment engine handles the heavy lifting. A critical design decision here is that `CheckCash` will automatically create a trust line between the destination and the issuer if one doesn't exist yet, provided the destination has sufficient reserve. This is justified by the fact that the destination is actively signing the transaction — they clearly want the funds. The trust line's limit is then **temporarily set to `cMaxValue`** (the maximum possible `STAmount`) during the `flow()` call, regardless of whatever limit the destination may have previously set. A `scope_exit` guard restores the original limit once `flow()` returns. This design choice avoids the scenario where the destination's own trust line limit would block them from receiving funds they explicitly requested.

For `sfDeliverMin` with IOUs, the "ask" amount passed to `flow()` is `cMaxValue / 2`. This upper bound exists to tolerate gateway transfer rates: since transfer rates cannot exceed 200%, dividing by two ensures that even with the maximum markup the actual delivery cannot overflow the `STAmount` representation.

For MPT assets without an existing MPT holding on the destination, `checkCreateMPT()` is called to initialize the holding slot, mirroring the trust line creation logic.

After a successful transfer, the check is removed from both the owner and destination account directories via `dirRemove`, the source's owner count is decremented with `adjustOwnerCount`, and the check SLE itself is erased.

## Relationship to Sibling Transactors

All three check transactors (`CheckCreate`, `CheckCash`, `CheckCancel`) share the same structural skeleton and all declare `ConsequencesFactory{Normal}`. `CheckCreate` and `CheckCash` both override `checkExtraFeatures` to gate MPT support on `featureMPTokensV2`; `CheckCancel` does not, treating cancellation as amendment-agnostic. `CheckCash` is the only one that invokes `flow()` and manages the `PaymentSandbox` pattern — consistent with it being the only transactor in the group that performs a value transfer rather than pure ledger object lifecycle management.