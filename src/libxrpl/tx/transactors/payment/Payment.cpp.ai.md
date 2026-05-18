# `Payment.cpp` — XRP Ledger Payment Transaction Transactor

## Role in the System

`Payment.cpp` implements the `Payment` transaction type, the most fundamental and feature-rich transaction on the XRP Ledger. It handles three structurally distinct cases under one umbrella: direct XRP-to-XRP transfers, multi-hop IOU/token payments routed through the DEX-like path engine, and direct Multi-Purpose Token (MPT) transfers. Because of this breadth the file is the largest single transactor in the codebase, and it contains the most elaborate feature-gating logic.

The class inherits from `Transactor`, which enforces a strict multi-phase pipeline. Each phase has a different view of the world and a different contract about what it may read or write.

## Transaction Pipeline

**`makeTxConsequences()`** executes before ledger state is read. Its only job is to compute the worst-case XRP spend for fee-reservation purposes: if `sfSendMax` is present and native, that amount is the ceiling; otherwise `sfAmount` is used. Non-XRP payments return `beast::zero`.

**`checkExtraFeatures()`** gates two optional fields behind amendments: `sfCredentialIDs` requires `featureCredentials`, and `sfDomainID` requires `featurePermissionedDEX`. This is the canonical place to add similar guards for any future optional field.

**`getFlagsMask()`** is context-sensitive. Under `MPTokensV1` without `MPTokensV2`, direct MPT payments are limited to `tfUniversal | tfPartialPayment`; all other payment modes get the full `tfPaymentMask`. This avoids exposing flags that have no meaning yet for token types that don't support path-based routing.

**`preflight()`** performs entirely stateless structural validation. A notable design is `getMaxSourceAmount()`, a free helper that reconstructs the sender's source ceiling. When no `sfSendMax` is present and the destination asset is an IOU, the helper builds a new `STAmount` with the *sender's* account as issuer rather than the destination's issuer. This matters because IOU amounts are trust-line-scoped: the "same" asset from two different issuers is not fungible.

`preflight()` then enforces a matrix of incompatible flag/field combinations: XRP-to-XRP payments may not carry `sfSendMax`, `sfPaths`, `tfPartialPayment`, `tfLimitQuality`, or `tfNoRippleDirect`, because those features only make sense when routing through intermediaries. MPT direct payments (pre-V2) are similarly forbidden from carrying `sfPaths`. Self-payments are rejected unless `sfPaths` is present — which would indicate an attempted arbitrage cycle, which is permitted.

The `sfDeliverMin` field is validated to be positive, have the same asset as `sfAmount`, not exceed `sfAmount`, and only appear when `tfPartialPayment` is set. This combination forms the "at least this much must arrive" guarantee.

**`checkPermission()`** handles delegated transactions — where a separate delegate account authorises the transaction on behalf of the source account. It first attempts coarse-grained permission via `checkTxPermission()`. If that fails it loads granular permissions and checks two specific types: `PaymentMint` (the source is the token issuer — sending tokens into existence) and `PaymentBurn` (the destination is the issuer — sending tokens back to be destroyed). Critically, granular permissions are only valid for *direct* payments: if `sfPaths` is present or `sfSendMax` names a different asset than `sfAmount`, the check immediately returns `terNO_DELEGATE_PERMISSION`. This prevents a delegate from routing funds through currency conversions it was never authorised to perform.

**`preclaim()`** reads ledger state without modifying it. It validates: whether the destination account exists (non-native payments fail hard with `tecNO_DST` if it doesn't, while XRP payments may create the account if the amount exceeds the base reserve); whether the destination requires a `DestinationTag` and one is missing; whether the path set exceeds the hard limits (`MaxPathSize = 6` paths, `MaxPathLength = 8` hops); whether `sfCredentialIDs` entries are valid on-ledger; and, for `sfDomainID` payments, whether both source and destination are members of the specified permissioned DEX domain.

The comment `// TODO: de-dupe` at line 323 flags a known maintenance debt — the reserve-insufficiency check for destination creation is handled slightly differently from the analogous check in `doApply()`.

## `doApply()` — Three Execution Paths

The routing decision that drives `doApply()` is computed on a single boolean:

```cpp
bool const ripple = (hasPaths || sendMax || !dstAmount.native()) && (!isDstMPT || MPTokensV2);
```

This selects the path engine only when the payment isn't a plain XRP transfer *and* isn't a pre-V2 MPT transfer.

### IOU / Path-Based Payments

When `ripple` is true, `doApply()` verifies deposit pre-authorisation, constructs a `path::RippleCalc::Input` struct, then calls `RippleCalc::rippleCalculate()` inside a `PaymentSandbox`. The sandbox is a copy-on-write overlay over the current ledger view; `pv.apply(ctx_.rawView())` propagates mutations only if processing reaches that line — meaning a mid-payment failure leaves the ledger untouched.

After `RippleCalc` returns, if the actual delivery `rc.actualAmountOut` differs from `dstAmount`, there are two cases: if `sfDeliverMin` is set and the actual amount falls short, the result is `tecPATH_PARTIAL`; otherwise `ctx_.deliver()` records the actual delivered amount for the transaction metadata. The "delivered amount" metadata field is crucial for partial payment detection by downstream consumers.

A subtle but important policy: if `RippleCalc` returns any `ter*` retry code, it is converted to `tecPATH_DRY`. This charges the fee instead of granting a free retry. The comment explains the rationale — the overhead of running the path engine has already been incurred, and charging a fee discourages users from submitting poorly-constructed path specs.

### Direct MPT Payments (pre-V2)

When the destination is an `MPTIssue` and `featureMPTokensV2` is not enabled, the file handles MPT payments directly without the path engine. The logic:

1. Validates that both accounts hold or are authorised for the MPT issuance via `requireAuth()`.
2. Calls `canTransfer()` to check any transfer restrictions on the issuance.
3. Checks deposit pre-authorisation.
4. Computes the transfer rate from `transferRate()` and multiplies by `dstAmount` to get `requiredMaxSourceAmount`. The comment acknowledges that the rounding here will change once MPT is integrated into the DEX.
5. If `tfPartialPayment` is set and the sender can't cover the full source amount, it scales down `amountDeliver` proportionally.
6. If either `requiredMaxSourceAmount > maxSourceAmount` or the scaled delivery is below `sfDeliverMin`, it returns `tecPATH_PARTIAL`.
7. Executes via `accountSend()` inside a `PaymentSandbox`.

The freeze check (`isAnyFrozen()`) is deliberately skipped when one party is the issuer — issuers can always send to holders and holders can always return to issuers, even when frozen.

The `fixMPTDeliveredAmount` amendment gates the `ctx_.deliver()` call for MPT partial payments. This matches the pattern used for IOU payments, where delivered amount tracking was also introduced retroactively.

### Direct XRP Payments

The simplest path. It checks source balance against `dstAmount + reserve`, accounting for whether the source account is also the fee payer (in delegated transactions the fee comes from the delegate, not the source). The deposit authorisation check has a special bypass (Rule 3): if the destination's balance is at or below the base reserve *and* the payment amount is also at or below the base reserve, the check is waived. This prevents an account from entering an unrecoverable state where it has set `lsfDepositAuth`, spent all its XRP, and can no longer receive funds to pay for transactions to unset the flag.

The code guards against payments to pseudo-accounts (AMMs, vaults, etc.) via `isPseudoAccount(sleDst)`. This guard is not amendment-gated because pseudo-account status is determined by discriminator fields that are themselves amendment-gated, so the behaviour is implicitly correct across all amendment combinations.

After the balance arithmetic, `lsfPasswordSpent` is cleared on the destination if it was set — a legacy mechanism from the original password-based account creation flow.

## Design Observations

The three-way dispatch inside `doApply()` rather than three separate transactor subclasses is a deliberate choice: the XRPL transaction format uses a single `ttPAYMENT` type identifier, so all payment semantics must live in one transactor. The cost is complexity in `preflight()` and `doApply()`; the benefit is that adding new payment variants (e.g., enabling MPTs in the path engine under V2) requires only local changes to the routing logic rather than a new transaction type.

The `PaymentSandbox` pattern applied to both path-based and MPT payments — but not to direct XRP — reflects their relative complexity: XRP arithmetic is trivially reversible (two balance field writes), while path-based and MPT payments can touch many ledger objects and must appear atomic.