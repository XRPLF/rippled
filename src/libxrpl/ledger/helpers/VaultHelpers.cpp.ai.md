# `VaultHelpers.cpp` — Asset/Share Conversion Math for XRPL Single Asset Vaults

This file is the arithmetic core of XRPL's Single Asset Vault feature (also called the Lending Protocol). It implements four pure conversion functions that translate between a depositor's assets and the MPT-based vault shares they receive or redeem. Every `VaultDeposit` and `VaultWithdraw` transaction calls into these helpers to determine the exact exchange amounts before any ledger state is modified.

## The Vault Share Model

A Single Asset Vault accepts deposits of one underlying asset (XRP, an IOU, or an MPT) and mints proportional shares as an MPTokenIssuance. Each function receives two `SLE` (Serialized Ledger Entry) objects: the `vault` entry, which tracks the vault's total deposited assets (`sfAssetsTotal`) and unrealized losses (`sfLossUnrealized`), and the `issuance` entry for the share MPT, which tracks the current outstanding share supply (`sfOutstandingAmount`). The functions read these fields and perform the proportional exchange calculations.

## Deposit vs. Withdrawal: The Loss Accounting Split

The design choice that most distinguishes this file is the asymmetry between deposit and withdrawal functions. The deposit pair (`assetsToSharesDeposit`, `sharesToAssetsDeposit`) uses the raw `sfAssetsTotal` as the vault's total. The withdrawal pair (`assetsToSharesWithdraw`, `sharesToAssetsWithdraw`) subtracts `sfLossUnrealized` before using the total:

```cpp
Number assetTotal = vault->at(sfAssetsTotal);
assetTotal -= vault->at(sfLossUnrealized);
```

This is the mechanism by which unrealized losses are socialised across all shareholders at withdrawal time. When a borrower defaults (or a loan is marked down), `sfLossUnrealized` grows. A departing depositor redeems their shares at the lower net asset value, which correctly reflects their pro-rata share of that loss. New depositors, however, are priced against the full gross `sfAssetsTotal` — their mint-in rate is based on the vault's book value rather than market value, a deliberate design choice that keeps the deposit formula consistent with vault accounting.

## The Empty-Vault Bootstrap

When `assetTotal == 0`, the vault has no assets and no shares can exist yet. Both deposit functions handle this case specially rather than dividing by zero:

```cpp
if (assetTotal == 0)
{
    return STAmount{
        shares.asset(),
        Number(assets.mantissa(), assets.exponent() + vault->at(sfScale)).truncate()};
}
```

The `sfScale` field shifts the exponent of the incoming asset amount upward when computing the initial share allocation. This establishes the initial exchange rate as 1 asset = 10^scale shares, providing the vault operator a lever to set granularity. Scaling up the share count relative to the underlying asset reduces the impact of the classic "first depositor donation attack," where a malicious first depositor donates a tiny amount directly to inflate the share price and cause rounding losses for subsequent depositors.

For withdrawals on an empty vault, both functions simply return zero — either zero shares or zero assets — which is correct: there is nothing to redeem.

## Truncation Policy and `TruncateShares`

Because share MPTs are integers (no fractional tokens), every result must be a whole number. The deposit direction always truncates: `assetsToSharesDeposit` calls `.truncate()` on the computed `Number` before returning it. This is vault-favorable — the depositor receives slightly fewer shares, and the rounding residue stays in the vault, protecting existing shareholders from dilution.

The withdrawal direction introduces a choice via `TruncateShares`:

```cpp
enum class TruncateShares : bool { no = false, yes = true };
```

`assetsToSharesWithdraw` (converting an asset amount into the share cost for that withdrawal) truncates by default but supports `TruncateShares::yes` to force truncation. Callers use `TruncateShares::yes` when they need to find the minimum number of shares that covers a given asset withdrawal — for example, when checking if the user holds enough shares before committing the transaction. `sharesToAssetsWithdraw` (converting shares to assets) does not truncate: the user gets the full fractional asset value, which is then rounded by `STAmount` construction through XRPL's `Number` type.

## Validation Architecture

Each function opens with a two-tier guard. First, `XRPL_ASSERT` fires in debug builds if either the input is negative or the asset type doesn't match what the vault expects. Second, an identical runtime `if` guard immediately returns `std::nullopt` for the same conditions. This double-check pattern means the assert catches logic errors during development while the `std::nullopt` path gives the caller a clean failure signal in production — callers like `VaultDeposit::doApply` treat `nullopt` as `tecINTERNAL`. The redundant `// LCOV_EXCL_LINE` comments on the `nullopt` returns reflect that these branches are unreachable in correctly-functioning code and are intentionally excluded from coverage reports.

The functions do not null-check the `shared_ptr` parameters themselves; `SLE` access through `->at()` is expected to succeed because callers have already fetched and validated the ledger entries in `preclaim`. This is a deliberate RAII contract: by the time these helpers are invoked, the state preconditions have been established.

## Relationship to Callers

`VaultDeposit::doApply` calls `assetsToSharesDeposit` before transferring any balances — the computed share count is stored first, then the actual asset transfer and MPT minting follow. `VaultWithdraw` calls both `sharesToAssetsWithdraw` (to compute the redemption value of a share-denominated withdrawal) and `assetsToSharesWithdraw` (to compute the minimum shares required for an asset-denominated withdrawal). This separation of the conversion step from the state-mutation step is deliberate: it ensures the exchange rate snapshot is taken at a single point in time and cannot drift mid-transaction.