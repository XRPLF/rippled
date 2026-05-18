# `VaultWithdraw.cpp` — Vault Withdrawal Transactor

## Role in the System

`VaultWithdraw` is the XRPL transactor that implements the `VaultWithdraw` transaction type, allowing vault participants to redeem shares and receive underlying assets in return. A vault on the XRPL is a pool that holds a single asset type and issues MPT-based shares proportionally. This file is the structural mirror of `VaultDeposit.cpp`: where deposit converts assets into shares, withdrawal converts shares back into assets. It follows the standard three-phase transactor lifecycle — `preflight`, `preclaim`, and `doApply` — with complexity concentrated in the application phase where the share-to-asset exchange rate is computed.

## Preflight: Stateless Sanity Checks

`preflight` performs pure-transaction validation with no ledger access. It rejects a zero `sfVaultID` (a null key is always a programming error), a non-positive `sfAmount` (withdrawing zero or negative amounts is semantically meaningless), and a zero `sfDestination` if present (the optional destination field, when supplied, must be a real account). These checks are cheap and deterministic — they run before any ledger state is examined.

## Preclaim: Contextual Validation

`preclaim` validates the transaction against live ledger state. It fetches the vault SLE by ID and fails with `tecNO_ENTRY` if it doesn't exist. It then verifies that the requested amount is denominated in either the vault's underlying asset (`sfAsset`) or the vault's share MPT (`sfShareMPTID`), returning `tecWRONG_ASSET` if neither matches. The vault's `sfWithdrawalPolicy` must be `vaultStrategyFirstComeFirstServe` — the only currently supported policy; other values hit `tefINTERNAL` guarded by `LCOV_EXCL` markers, indicating unreachable code under normal conditions.

### Amendment: `fixSecurity3_1_3` and the Share-Limit Gap

An important security fix is embedded in `preclaim`. The `canWithdraw` function checks whether the destination account would exceed its asset-holding limit (IOU trust line maximum or MPT `MaximumAmount`). Before the `fixSecurity3_1_3` amendment, if a user specified the withdrawal amount in shares rather than assets, this limit check was silently skipped — the code path went directly to the simpler `canWithdraw(ctx.view, ctx.tx)` overload. Post-amendment, when the amount is share-denominated, the code first calls `sharesToAssetsWithdraw` to compute the equivalent asset amount, then feeds that result into the full `canWithdraw(view, from, to, amount, hasDestinationTag)` overload. The explicit `overflow_error` catch around this conversion returns `tecPATH_DRY` rather than crashing — a deliberate choice given that large `sfScale` values make overflow arithmetically trivial.

### Authorization and Freeze Checks

`preclaim` enforces a two-tier authorization model. If the transaction sends assets to the submitting account's own wallet, `WeakAuth` is used for `requireAuth` — the system will create an MPToken or trust line on the submitter's behalf in `doApply`. If `sfDestination` specifies a third-party account, `StrongAuth` is required, meaning the token holding must already exist. Two separate `checkFrozen` calls follow: one on the vault's asset for the destination account (you cannot receive a frozen IOU or locked MPT), and one on the vault's share MPT for the submitting account (you cannot surrender shares that are frozen/locked on your side).

## `doApply`: The Exchange Rate Logic

The application phase peeks the vault SLE for mutation, resolves the share MPT issuance, and then determines how many shares to burn and how many assets to release. Two exchange modes exist:

**Asset-denominated mode** (`amount.asset() == vaultAsset`): The user specifies a fixed asset quantity to receive. The code calls `assetsToSharesWithdraw` to determine how many integer shares must be redeemed to cover that amount, then immediately calls `sharesToAssetsWithdraw` on the resulting integer share count to get the actual asset amount to disburse. This double-conversion is not redundant — it is a deliberate rounding correction. Because shares are MPT (integer-only), converting assets-to-shares truncates fractional parts. Re-converting shares-to-assets computes the precise asset amount the vault will actually release for those integer shares. If the first conversion produces zero shares, `tecPRECISION_LOSS` is returned; the deposit was too small to represent even one share unit.

**Share-denominated mode** (`amount.asset() == share`): The user specifies an exact share count to burn. `sharesToAssetsWithdraw` directly computes the corresponding asset payout. No rounding step is needed here since the share count is already integral.

Both modes catch `std::overflow_error` and return `tecPATH_DRY` with a debug log that includes the vault's `sfScale`, `sfAssetsTotal`, and the issuance's `sfOutstandingAmount`. These fields are logged at `debug` rather than `error` level precisely because large-scale vaults make overflow easily triggerable by legitimate users.

### Liquidity Gating via `sfAssetsAvailable`

After computing the exchange, `doApply` checks `sfAssetsAvailable` rather than the vault pseudo-account's raw balance. The distinction matters: a vault may have pledged assets to lending brokers, reducing what can actually be paid out on demand. `sfAssetsAvailable` tracks only the assets currently liquid in the vault, not the total it is entitled to. If available assets are insufficient, the withdrawal returns `tecINSUFFICIENT_FUNDS`. Upon success, both `sfAssetsTotal` and `sfAssetsAvailable` are decremented by `assetsWithdrawn`.

### Share Redemption and MPToken Cleanup

Shares flow from the submitter back to the vault pseudo-account via `accountSend(..., WaiveTransferFee::Yes)`. Transfer fees are waived on both legs of vault share movement, consistent with the vault's role as a protocol-level construct rather than an end-user token.

After redeeming shares, if the submitter's share balance drops to zero and they are not the vault owner, the code attempts to remove the now-empty MPToken holding with `removeEmptyHolding`. Vault owners are explicitly excluded from this cleanup because they may need the MPToken alive for future share issuances. A result of `tecHAS_OBLIGATIONS` is silently ignored — the MPToken has non-zero associated obligations and must persist.

### Private Vaults and Indefinite Authorization

A notable design choice is the explicit comment that `doApply` does not check `lsfVaultPrivate` on the vault. Possession of shares is treated as proof of prior authorization: if you ever deposited into a private vault, you must have been authorized at that time. This means access to a private vault is effectively irrevocable once granted via share ownership. Withdrawal privileges are tied to share possession, not to ongoing credential validity.

### Completion

`doApply` concludes by calling `associateAsset(*vault, vaultAsset)` — a per-SLE hook that triggers any `STNumber` field rounding to the correct decimal scale for the vault's asset — before delegating the actual asset credit to `doWithdraw` from `View.h`. `doWithdraw` handles trust line or MPToken creation for the destination account (when withdrawing to self under `WeakAuth`), enforces deposit authorization checks for third-party recipients, and executes the final `accountSend` from the vault pseudo-account to the destination.

## Key Relationships

`VaultWithdraw.cpp` depends heavily on `VaultHelpers.h` for the four directional conversion functions (`assetsToSharesWithdraw`, `sharesToAssetsWithdraw`) which encapsulate the vault's exchange rate math using `sfAssetsTotal`, `sfOutstandingAmount`, and `sfScale`. `TokenHelpers.h` provides `accountHolds`, `requireAuth`, `checkFrozen`, `accountSend`, and `removeEmptyHolding`. `View.h` provides `canWithdraw` and `doWithdraw` as the shared withdrawal authorization and execution primitives used across all vault-adjacent transactors. The symmetric counterpart `VaultDeposit.cpp` follows identical structural conventions and should be read alongside this file for a complete picture of the vault share lifecycle.