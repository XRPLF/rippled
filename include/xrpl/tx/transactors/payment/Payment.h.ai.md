# `Payment.h` — The Payment Transactor Interface

## Role in the System

`Payment.h` declares the `Payment` class, the transactor responsible for handling all XRPL `Payment` transaction type processing. It is one of the most structurally complex transactors in the codebase because a single `Payment` transaction must handle three fundamentally different execution paths: direct XRP-to-XRP transfers, direct MPToken (MPT) transfers, and cross-currency path-based payments routed through the DEX via `RippleCalc`. The header is minimal by design — it declares the interface mandated by the `Transactor` framework while hiding all logic in the `.cpp`.

## Inheritance and the Transactor Pipeline

`Payment` inherits publicly from `Transactor`, which provides a three-stage transaction processing pipeline invoked via static template dispatch rather than virtual dispatch. The base `Transactor::invokePreflight<T>` template calls `T::checkExtraFeatures`, then the internal `preflight1` (sanity and flags), then `T::preflight`, then `preflight2` (signature validation), and finally `T::preflightSigValidated`. This compile-time polymorphism means that overriding pipeline steps requires only declaring the right static method with the right name — there is no vtable involved at the preflight and preclaim stages. Only `doApply` is a true virtual override.

## `ConsequencesFactory` and `makeTxConsequences`

The class declares `ConsequencesFactory{Custom}`, distinguishing it from transactors that use the `Normal` or `Blocker` factory. This flag causes the framework to invoke `Payment::makeTxConsequences` to compute the potential maximum XRP spend for fee-level ordering purposes. The implementation inspects `sfSendMax`: if it is present and denominated in XRP, that value becomes the maximum; otherwise it falls back to `sfAmount`. If neither is XRP, the maximum spend is zero. This matters for transaction consequence ordering — the engine needs a conservative upper bound on how much XRP could be consumed before it will apply the transaction.

## Preflight Validation

`Payment::preflight` is stateless and performs purely structural validation against the serialized transaction fields. It enforces several invariants:

- MPT amounts in `sfAmount` require the `featureMPTokensV1` amendment to be active; without it, any MPT-denominated payment is rejected as `temDISABLED`.
- Direct XRP payments cannot carry `sfSendMax` (redundant), `sfPaths` (inapplicable), `tfPartialPayment` (meaningless), `tfLimitQuality` (meaningless), or `tfNoRippleDirect` (meaningless). These are rejected as distinct `temBAD_SEND_XRP_*` codes rather than a generic malformation, giving clients precise diagnostic information.
- When `sfDeliverMin` is specified, `tfPartialPayment` must also be set, `DeliverMin` must be positive, its asset must match `sfAmount`'s asset, and it must not exceed `sfAmount`. This prevents a scenario where `DeliverMin` is set but cannot be semantically fulfilled.
- Self-payments without paths are rejected as `temREDUNDANT`. A self-payment with paths is allowed because it could represent an arbitrage cycle.

`checkExtraFeatures` gates two optional fields: `sfCredentialIDs` requires the `featureCredentials` amendment, and `sfDomainID` requires `featurePermissionedDEX`. This pattern cleanly separates feature-gating from structural validation.

`getFlagsMask` is non-trivial: for MPT payments when `featureMPTokensV2` is not enabled, only `tfPartialPayment` is allowed beyond the universal flags. Once `MPTokensV2` is active, the full `tfPaymentMask` (which includes `tfLimitQuality`, `tfNoRippleDirect`, etc.) becomes valid, since MPT payments can then participate in path-finding.

## Preclaim Checks

`preclaim` runs with ledger read access and enforces runtime conditions that depend on current ledger state:

- If the destination account does not exist and the payment is non-native (IOU or MPT), the transaction is rejected `tecNO_DST` rather than creating an account — only XRP can fund a new account. If the amount is XRP but below the base reserve, `tecNO_DST_INSUF_XRP` is returned.
- If the destination has `lsfRequireDestTag` set, `sfDestinationTag` must be present or the transaction fails `tecDST_TAG_NEEDED`.
- Path complexity is bounded here: paths exceeding `MaxPathSize = 6` or any individual path exceeding `MaxPathLength = 8` steps are rejected `telBAD_PATH_COUNT`. These constants live as private `static constexpr` members, documenting the limits as invariants of the class rather than magic numbers.
- Credential validity is checked, and `sfDomainID` (Permissioned DEX) is validated to confirm both sender and destination are members of the specified domain.

## Delegate Permission Model

`checkPermission` implements a layered delegation check. When a transaction carries `sfDelegate`, the method reads the delegate authorization ledger entry and applies two tiers:

1. Full transaction permission: if the delegate SLE grants blanket `ttPAYMENT` permission, the payment proceeds.
2. Granular permissions: if full permission is not granted, `PaymentMint` allows a delegate to issue tokens where the delegating account is the issuer, and `PaymentBurn` allows a delegate to send tokens back to their issuer. Crucially, both granular permissions are only valid for *direct* payments — any payment with `sfPaths` set or with `sfSendMax` pointing to a different asset than `sfAmount` is denied `terNO_DELEGATE_PERMISSION`. This prevents granular grants from being leveraged to manipulate multi-hop paths where intermediate assets could be exploited.

## `doApply` — Three Execution Paths

The execution logic in `doApply` branches into three distinct paths based on asset type and flags:

**RippleCalc path-based payments**: Any payment involving `sfPaths`, `sfSendMax`, or a non-native destination amount (except direct MPT without `MPTokensV2`) is routed through `path::RippleCalc::rippleCalculate`. All mutations are staged in a `PaymentSandbox` view and applied atomically only if the calculation succeeds. If `RippleCalc` returns a `TerRetry` code, the transactor converts it to `tecPATH_DRY` to ensure fee consumption — a retry code would prevent fee collection, so the conversion is a deliberate economic defense against path-spam attacks. The actual delivered amount is set via `ctx_.deliver()` when the delivered amount differs from the requested amount, enabling the `DeliveredAmount` metadata field.

**Direct MPT payments** (when `featureMPTokensV2` is not active): These bypass `RippleCalc` entirely. The transactor checks authorization (`requireAuth`), transfer permission (`canTransfer`), and frozen state before computing the transfer rate. Partial payment is handled manually: if `tfPartialPayment` is set and the sender cannot cover the full transfer-rate-adjusted cost, the deliverable amount is scaled down. The `deliverMin` floor is then checked against the scaled amount. The fix `fixMPTDeliveredAmount` gates whether the `DeliveredAmount` metadata field is updated for MPT transfers that differ from their nominal amount.

**Direct XRP payments**: The simplest path validates that the sender has sufficient balance above their owner reserve (factoring in whether the sender or a delegate account is the fee payer), rejects pseudo-accounts as recipients, checks deposit pre-authorization (with a bypass for small amounts to prevent accounts from becoming permanently wedged with no XRP to pay fees), then directly updates both SLE `sfBalance` fields.

## Design Rationale

The separation of `preflight` (stateless), `preclaim` (read-only ledger), and `doApply` (mutating) reflects the XRPL consensus engine's need to evaluate transactions at different points in the validation pipeline without always completing the full execution. The three payment modes within a single transactor class, rather than three separate transactors, reflect that from the perspective of the protocol and users, `Payment` is a single transaction type — the internal branching is an implementation detail hidden from the serialized transaction format and the validation framework.