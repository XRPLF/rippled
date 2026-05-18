# `AccountRootHelpers.cpp` — AccountRoot Ledger Object Utilities

This file implements the stateless helper functions declared in `AccountRootHelpers.h` for reading from and writing to `AccountRoot` ledger objects. It covers the full surface area of account-level concerns: freeze state, spendable balance, owner count bookkeeping, transfer fees, destination tag enforcement, and the creation and detection of pseudo-accounts. It sits at the intersection of the read-only `ReadView` and the mutable `ApplyView` interfaces, and nearly every transaction processor touches at least one function here.

## Owner Count Arithmetic: `confineOwnerCount`

The file-private `confineOwnerCount()` is the foundation for two public functions. It accepts the current `uint32_t` owner count and a signed adjustment, deliberately relying on **well-defined unsigned overflow/underflow semantics** to detect boundary violations rather than doing range-checked signed arithmetic. If `adjusted < current` after a positive `adjustment`, unsigned wrap has occurred; if `adjusted > current` after a negative `adjustment`, unsigned underflow has occurred. Both are clamped — overflow to `UINT32_MAX`, underflow to `0` — and a fatal-level log is emitted when an `AccountID` is provided. The optional `id` parameter is a design choice that allows `xrpLiquid` to call the function without an account ID (speculative call for reserve computation), while `adjustOwnerCount` always passes one for full diagnostics.

## Liquid XRP Calculation: `xrpLiquid`

`xrpLiquid()` answers the question "how much XRP can this account freely spend right now?" Its formula is straightforward: `max(0, balance − reserve)`. What makes the implementation non-obvious is that both the balance read and the owner-count read route through virtual hook methods on the view:

```cpp
confineOwnerCount(view.ownerCountHook(id, sle->getFieldU32(sfOwnerCount)), ownerCountAdj);
// ...
view.balanceHookIOU(id, xrpAccount(), fullBalance);
```

In normal transaction processing, `ownerCountHook` and `balanceHookIOU` are identity functions that return their arguments unchanged. But when called from within a `PaymentSandbox`, they return conservative values that account for credits and owner-count changes made earlier in the same payment path. This hook-dispatch design lets `xrpLiquid` serve both contexts without branching on payment vs. non-payment logic.

Pseudo-accounts receive special treatment: `isPseudoAccount(sle)` bypasses the reserve calculation entirely (`XRPAmount{0}`), because protocol-controlled accounts are not subject to base and owner reserve requirements. Normal accounts clamp the result at zero to prevent a negative spendable balance from being returned.

## Owner Count Write Path: `adjustOwnerCount`

`adjustOwnerCount()` is the write-side counterpart. After computing the new count through `confineOwnerCount`, it calls `view.adjustOwnerCountHook()` before writing to the SLE. `PaymentSandbox` overrides this hook to record the high-water-mark owner count — since payments can only decrease owner counts, the maximum observed count is the conservative bound used by `ownerCountHook` on subsequent reads within the same payment. This pairing of `adjustOwnerCountHook` and `ownerCountHook` prevents a transient count reduction mid-payment from bypassing reserve checks.

## Pseudo-Account Infrastructure

The pseudo-account system is the most architecturally significant responsibility in this file. Pseudo-accounts are `AccountRoot` objects controlled entirely by protocol logic rather than by a private key. They are used today for AMM pools (`sfAMMID`), single-asset Vaults (`sfVaultID`), and Loan Brokers (`sfLoanBrokerID`).

**Address generation** (`pseudoAccountAddress`) derives a candidate `AccountID` from the parent object's key by hashing `(attempt_index, ledger_parentHash, pseudoOwnerKey)` through `sha512Half` then `ripesha_hasher`. Incorporating the ledger's `parentHash` prevents precomputation of collisions. The loop retries up to `maxAccountAttempts = 256` times — this constant is annotated as immutable without an amendment, since changing it would alter the address space. On exhaustion it returns `beast::zero`; `createPseudoAccount` propagates this as `tecDUPLICATE`.

**Field-based pseudo detection** (`getPseudoAccountFields`, `isPseudoAccount`) relies on a metadata flag on `SField` definitions: `SField::sMD_PseudoAccount`. Any `SField` carrying that flag is a pseudo-account designator. `getPseudoAccountFields` builds and caches the authoritative list at first call by scanning the `ltACCOUNT_ROOT` SOTemplate from `LedgerFormats::getInstance()`. `isPseudoAccount` then checks whether any of those fields is present on a given SLE, with an optional filter to test for a specific pseudo-account type. The comment in the implementation explicitly names this "defensive coding" — the null check and `ltACCOUNT_ROOT` type guard are included even when callers might already guarantee them, keeping the semantics of a `true` return value unambiguous.

**Account creation** (`createPseudoAccount`) assembles the `AccountRoot` SLE with:
- Zero balance and sequence number `0` (when `featureSingleAssetVault` or `featureLendingProtocol` is active) to make pseudo-accounts visually distinguishable and prevent them from submitting transactions even if the disable-master flag were somehow bypassed.
- `lsfDisableMaster | lsfDefaultRipple | lsfDepositAuth` — disabling the master key prevents all transaction submission; enabling default ripple allows trust-line flows for AMM and vault assets; deposit authorization blocks unsolicited incoming payments.
- The `ownerField` back-link stores the parent object's key in the pseudo-account's ledger entry. The `XRPL_ASSERT` in `createPseudoAccount` verifies the caller passed a field that actually carries `sMD_PseudoAccount`, catching misuse at debug time.

## Remaining Utilities

`isGlobalFrozen` guards against operating on XRP (which cannot be frozen) before reading the `lsfGlobalFreeze` flag, returning `false` for non-existent accounts rather than asserting. `transferRate` defaults to `parityRate` (one billion, representing 1:1) when no `sfTransferRate` field is present, ensuring the caller never has to handle a missing-fee case. `checkDestinationAndTag` centralises the two-step destination validation — existence check followed by `lsfRequireDestTag` enforcement — that would otherwise be repeated across all payment-adjacent transactors.