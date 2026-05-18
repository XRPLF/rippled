# AMMClawback.cpp

## Role in the System

`AMMClawback.cpp` implements the `ttAMM_CLAWBACK` transaction, which lets a token issuer reclaim their own assets from an AMM liquidity pool that a particular account (the *holder*) has a position in. This transaction fills a regulatory gap that existed when the original AMM feature launched: issuers with clawback authority over their tokens had no mechanism to recover those assets once they had been deposited into an AMM pool, because the pool's LP tokens aren't the underlying token and the normal `Clawback` transaction only operates on trust lines. The `featureAMMClawback` amendment introduced this transactor to close that gap by layering a forced withdrawal on top of the existing AMM infrastructure.

The file lives in the `dex/` group alongside `AMMWithdraw`, `AMMDeposit`, and the other AMM transactors. It is a heavy consumer of `AMMWithdraw`'s static helpers rather than reimplementing withdrawal math itself.

## Transaction Fields and Flags

The transaction identifies the operation through five fields: `sfAccount` (the issuer initiating the claw), `sfHolder` (the LP whose position will be unwound), `sfAsset` (the issuer's own token — always required), `sfAsset2` (the pool's paired asset), and an optional `sfAmount` (a cap on how much of `sfAsset` to recover). One flag is defined: `tfClawTwoAssets`, which extends the claw to include `sfAsset2` in addition to `sfAsset`.

## Validation Pipeline

`checkExtraFeatures` acts as a feature gate. It returns `false` — blocking the transaction entirely — if `featureAMMClawback` is not enabled. It also restricts MPT-denominated assets in any of the three amount fields to ledgers where `featureMPTokensV2` is active, preventing the newer token type from being used before the full MPT feature set has rolled out.

`preflight` performs all stateless structural checks. The issuer cannot equal the holder (a self-clawback makes no sense). `sfAsset` cannot be XRP since XRP has no issuer and cannot be subject to clawback. When `tfClawTwoAssets` is set, both `sfAsset` and `sfAsset2` must share the same issuer — the flag is only useful when the issuer controls both sides of the pool, and the framework enforces this rather than silently ignoring the flag for the uncontrolled asset. The optional `sfAmount`, if present, must refer to the same asset as `sfAsset` and must be positive.

`preclaim` performs ledger-state checks. Beyond verifying that both the issuer and holder accounts exist and that the AMM pool for the asset pair is present, it enforces permission requirements. The permission logic has a non-obvious branch: when `featureMPTokensV2` is **not** enabled and the issuer lacks `lsfAllowTrustLineClawback` or has set `lsfNoFreeze`, `preclaim` returns `tesSUCCESS` rather than an error code. This is an intentional soft-fail — the transaction is accepted by the network but becomes a no-op in `applyGuts` because no withdrawal path would be reached. This backward-compatible design avoids penalizing an issuer who submits the transaction on a network where the amendment has passed but whose account doesn't yet have the right flags set. When `featureMPTokensV2` is enabled, the per-asset `checkClawAsset` lambda takes over: for IOU assets it re-checks `lsfAllowTrustLineClawback` / `lsfNoFreeze` on the account; for MPT assets it checks the issuance-level `lsfMPTCanClawback` flag and verifies the issuance's `sfIssuer` matches the transaction submitter. The lambda cleanly handles the polymorphic `Asset` type via `visit()`.

## Application Logic

`doApply()` follows the standard XRPL sandbox pattern: it creates a `Sandbox` over the mutable view, delegates to `applyGuts()`, and only commits the sandbox to the raw view on success. This ensures that any partial failure leaves the ledger untouched.

`applyGuts()` begins, when the `fixAMMClawbackRounding` amendment is active, by reading the holder's LP token balance and passing it through `verifyAndAdjustLPTokenBalance`. This corrects accumulated floating-point rounding drift in the AMM's `LPTokenBalance` field — a known issue when a holder has been the sole LP for a long time — before the withdrawal math begins. The balance is then re-read after the adjustment because the helper may have modified the ledger entry.

The core withdrawal takes one of two paths depending on whether `sfAmount` is present:

- **No `sfAmount`**: The full position is liquidated. `AMMWithdraw::equalWithdrawTokens` is called with the holder's entire LP token balance, burning all their tokens and returning both assets proportionally. The trading fee is explicitly passed as `0` because this is not a voluntary withdrawal — there is no fee discount to model.

- **With `sfAmount`**: `equalWithdrawMatchingOneAmount` calculates the fraction of the pool that corresponds to the requested `sfAsset` amount. If the implied LP token withdrawal would exceed the holder's actual balance, it falls back to a full-position liquidation via `equalWithdrawTokens`. Otherwise it calls `AMMWithdraw::withdraw` with the computed token count and proportionally scaled `sfAsset2` amount. Here, `fixAMMClawbackRounding` adds another rounding pass: `getRoundedLPTokens` snaps the LP token count to a representable value, `adjustFracByTokens` re-derives the exact fraction from the snapped token count, and `getRoundedAsset` aligns both asset amounts. This prevents sub-dust residuals from stranding value in the pool.

After withdrawal, `AMMWithdraw::deleteAMMAccountIfEmpty` checks whether the new LP token balance is zero; if so, it tears down the AMM object and its associated account, keeping the ledger free of empty AMM entries.

The final transfer step uses `directSendNoFee` to move `amountWithdraw` (the recovered `sfAsset` amount) from the holder to the issuer. The second asset is only transferred if `tfClawTwoAssets` is set; absent that flag, `amount2Withdraw` is computed during withdrawal but simply left with the holder, preserving minimal disruption to the counter-party's position in cases where the issuer does not control `sfAsset2`.

## Design Observations

The decision to reuse `AMMWithdraw`'s static helpers — `equalWithdrawTokens`, `withdraw`, `deleteAMMAccountIfEmpty` — rather than duplicating the withdrawal mathematics is deliberate. The withdrawal invariants (constant-product formula, LP token burn, pool balance update) are complex and must be identical for all withdrawal paths. `AMMClawback` is architecturally a *policy layer* that determines *how much* to withdraw and *where the assets go* after withdrawal; the actual AMM state mutation is fully delegated.

The unconditional `tfee = 0` throughout `applyGuts` and `equalWithdrawMatchingOneAmount` reflects that the AMM trading fee was designed to compensate LPs for impermanent loss during voluntary swaps. A clawback is not a voluntary trade and the fee would be economically incoherent — it would partially shield the holder from a regulatory action by making recovery more expensive for the issuer.