# `VaultDeposit.cpp` — Vault Deposit Transactor

`VaultDeposit.cpp` implements the three-phase XRPL transaction logic for depositing assets into a vault ledger object. The vault primitive is a pooled-asset container: a depositor sends a fungible asset (XRP, IOU, or MPT) to the vault's pseudo-account and receives back vault *shares*, which are themselves an MPT issuance unique to that vault. This file contains the complete lifecycle — validation, state checks, and ledger mutation — for that exchange.

## Transaction Lifecycle

XRPL transactors divide their work across three static phases:

**`preflight`** runs before ledger access and validates only the raw transaction fields. Two checks apply: `sfVaultID` must not be `beast::zero` (an uninitialised key), and `sfAmount` must be strictly positive. These are syntactic guards; business logic is deferred.

**`preclaim`** performs read-only stateful validation against the ledger view. It resolves the vault SLE by `sfVaultID`, then enforces a cascade of invariants: the deposited asset must match `vault->at(sfAsset)`, the asset must be transferable from the depositor to the vault pseudo-account via `canTransfer`, neither the asset nor the vault shares can be frozen for the depositor, and the share MPT issuance must exist and not bear the `lsfMPTLocked` flag. The sanity check that vault shares and vault assets are different types is classified as an internal error (`tefINTERNAL`) and marked `LCOV_EXCL`, acknowledging it should be impossible given correct vault creation.

Private vault authorization deserves special attention. Because the vault's pseudo-account cannot sign transactions, it cannot use the standard MPT issuer-authorisation flow. Instead, private vault admission is governed by a `sfDomainID` on the share issuance, validated through `credentials::validDomain`. Critically, `preclaim` suppresses `tecEXPIRED` and only hard-fails other errors. This is intentional: expired credential cleanup is a side-effect that modifies ledger state, which is only permitted in `doApply`. If the domain check passes (or returns `tecEXPIRED`), the transaction proceeds; `doApply` later calls `enforceMPTokenAuthorization`, which handles expiry deletion. If no `sfDomainID` is present on a private vault, the call returns `tecNO_AUTH` — no fallback to direct issuer grants.

**`doApply`** is where all ledger mutations occur.

## MPToken Provisioning in `doApply`

Before computing the exchange, `doApply` must ensure the depositor holds an `MPToken` entry for the vault shares. The branching here reflects two modes:

- **Private vault, non-owner**: `enforceMPTokenAuthorization` is called. This checks (and potentially deletes) expired credentials and then creates or validates the `MPToken`.
- **Public vault, or vault owner**: `authorizeMPToken` is called unconditionally if the MPToken does not yet exist. Additionally, when the vault owner deposits into a *private* vault, a second `authorizeMPToken` call provisions an `MPToken` on the vault's pseudo-account (`sleIssuance->at(sfIssuer)`), authorised for the owner (`holderID = account_`). This is necessary because the vault's accounting infrastructure must itself be able to hold shares during certain operations, and the pseudo-account cannot self-authorise.

## Share/Asset Exchange Arithmetic

The exchange computation in `doApply` has a specific two-step structure that protects depositors from overpayment:

1. Call `assetsToSharesDeposit(vault, sleIssuance, amount)` — this converts the offered assets into shares, **truncating** (floor) the result since shares are integral MPT values. On a fresh vault with `sfAssetsTotal == 0`, the initial shares are seeded using the vault's `sfScale` field: `shares = floor(assets.mantissa * 10^(assets.exponent + scale))`. This seeds the exchange rate at creation time.

2. Call `sharesToAssetsDeposit(vault, sleIssuance, sharesCreated)` — this inverts the truncated share count back into an asset amount. The result `assetsDeposited` is guaranteed to be ≤ `amount` (an internal error fires if it is not), so the depositor is charged only what those exact shares correspond to — any fractional asset remainder stays with the depositor.

If `sharesCreated` rounds down to zero, the transaction returns `tecPRECISION_LOSS`, signalling that the deposit is too small relative to the current share price. Arithmetic overflow from `Number` with large `sfScale` values returns `tecPATH_DRY` with a debug-level log message rather than an error log, since this is a user-triggerable path.

## Ledger Mutations and Invariant Ordering

The ordering of state changes in `doApply` is deliberate:

```
vault->at(sfAssetsTotal) += assetsDeposited;
vault->at(sfAssetsAvailable) += assetsDeposited;
view().update(vault);
// Limit check against sfAssetsMaximum — BEFORE any transfer
accountSend(depositor → vaultAccount, assetsDeposited);   // assets in
// Negative balance sanity check
accountSend(vaultAccount → depositor, sharesCreated);     // shares out
associateAsset(*vault, vaultAsset);
```

The vault's totals are updated first so the `sfAssetsMaximum` cap can be enforced before any actual asset movement. If the deposit would exceed the maximum, the transaction fails with `tecLIMIT_EXCEEDED` at no cost to the depositor (the `view()` is rolled back by the framework). Both `accountSend` calls pass `WaiveTransferFee::Yes`, ensuring no transfer fees are levied on vault participants regardless of the asset's fee configuration. The negative-balance sanity check after the first `accountSend` is a last-resort internal guard against ledger corruption; it is also `LCOV_EXCL` because it should be unreachable under correct logic.

## Relationship to Sibling Transactors

`VaultWithdraw.cpp` is the mirror image of this file. Where `VaultDeposit` mints shares by calling `assetsToSharesDeposit` then verifies with `sharesToAssetsDeposit`, `VaultWithdraw` burns shares using `assetsToSharesWithdraw` and `sharesToAssetsWithdraw`, which additionally subtract `sfLossUnrealized` from `sfAssetsTotal` to account for off-chain losses reported by the vault operator. Notably, `VaultWithdraw` does not re-check `lsfVaultPrivate` — once you hold shares, you are considered permanently authorised to redeem them.