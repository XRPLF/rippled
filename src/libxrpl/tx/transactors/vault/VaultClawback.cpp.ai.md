# VaultClawback.cpp

`VaultClawback` implements the transactor for the `VaultClawback` transaction type in the XRPL vault system. A vault is a pooled on-ledger financial primitive where depositors exchange underlying assets for MPT-based share tokens representing their proportional claim. Clawback gives two distinct principals a way to reclaim value from a vault: the underlying asset's issuer can reclaim assets held in the vault (taking the depositor's proportional shares in exchange), and the vault owner can burn orphaned share tokens when a vault has no remaining assets. Both use cases share one transaction type because they follow the same fundamental ledger mechanics — destroy shares, optionally recover assets — but they differ substantially in who is permitted and under what conditions.

## The Three-Phase Transactor Pipeline

Like all XRPL transactors, `VaultClawback` separates validation into a stateless phase (`preflight`), a ledger-read phase (`preclaim`), and the mutation phase (`doApply`).

`preflight` handles the checks that require only the transaction fields themselves. A zero `sfVaultID` is rejected immediately as `temMALFORMED`. The optional `sfAmount` field, if present, must be non-negative and must not name XRP — vaults holding XRP cannot be subject to clawback, which is consistent with XRP's nature as the native ledger currency with no issuer.

`preclaim` loads the vault and share issuance SLEs and branches on the asset being named by the `sfAmount` field. A free helper, `clawbackAmount`, resolves which asset and quantity is implied when `sfAmount` is absent: if the transaction submitter is the vault owner, the implied zero-amount refers to shares (`sfShareMPTID`); otherwise it refers to the vault's underlying asset. This disambiguation is why `clawbackAmount` takes the submitter's `AccountID` — a zero amount has different semantics depending on who is asking.

One subtle edge case is explicitly guarded: when the vault owner is also the issuer of the vault's underlying asset, omitting `sfAmount` would be ambiguous between the share-burn path and the asset-clawback path. Rather than silently guess, `preclaim` rejects with `tecWRONG_ASSET`, forcing the caller to be explicit.

## Two Clawback Modes

**Asset issuer clawback** is the main use case. The issuer of the vault's underlying asset (IOU or MPT) can reclaim assets held inside the vault from a specific `sfHolder` account's position. The preclaim checks enforce that the asset is non-XRP, that the submitter is actually the asset's issuer (not just any participant), that the asset's issuance flags permit clawback (`lsfMPTCanClawback` for MPTs, or `lsfAllowTrustLineClawback` without `lsfNoFreeze` for IOUs), and that the issuer is not attempting to claw back from themselves.

The conversion arithmetic is isolated in `assetsToClawback`. This method converts the requested asset quantity into shares to destroy, then converts those shares back into assets to confirm the recoverable amount. When the caller specifies zero (meaning "all of holder's shares"), `sharesDestroyed` is determined from the holder's current balance via `accountHolds`. When a specific asset amount is given, the round-trip goes through `assetsToSharesWithdraw` first and then `sharesToAssetsWithdraw` — the double conversion exists because shares are integer-valued MPTs, so rounding from assets to shares and back yields the actual net asset recovery.

A critical safety clamp follows both paths: if the computed `assetsRecovered` exceeds `sfAssetsAvailable`, it is clamped to the available amount. Shares are then recomputed using `TruncateShares::yes` — deliberate truncation rather than rounding — to ensure that the corresponding assets derived from the truncated share count do not re-exceed the cap. The code then re-derives `assetsRecovered` from the truncated `sharesDestroyed` and double-checks the bound, treating any breach as an internal error rather than silently committing an over-recovery.

**Vault owner share burn** is a cleanup path. If a vault has outstanding share tokens but zero `sfAssetsTotal` and zero `sfAssetsAvailable`, the vault owner may use `VaultClawback` with a shares-denominated amount to burn a specific holder's shares. The check is strict: the vault must have no assets at all, and the amount (if non-zero) must equal exactly the holder's entire balance. This prevents partial burns, which would leave the share supply in an ambiguous state when the vault has no backing assets.

## Security Fix: `fixSecurity3_1_3`

The `assetsToClawback` method contains explicit branching on the `fixSecurity3_1_3` amendment. Before the amendment, a zero-amount clawback would convert the holder's full share balance to assets and return that amount directly — without clamping to `sfAssetsAvailable`. This allowed recovery of more assets than the vault actually had liquid, effectively bypassing any outstanding loans. The fix gate at line 233 preserves the old code path for ledger replay on historical ledgers, while all new transactions execute through the clamped path. The comment is explicit that the pre-fix behavior is retained "for ledger replay compatibility."

## `doApply` Execution Order

After validating with the pre-stored `sfAmount` and re-resolving `clawbackAmount`, `doApply` asserts that `sfLossUnrealized ≤ sfAssetsTotal − sfAssetsAvailable`, which is a structural invariant of the vault's accounting. The actual mutations proceed in a carefully ordered sequence: decrement `sfAssetsTotal` and `sfAssetsAvailable` on the vault SLE, call `view().update(vault)`, then `accountSend` to move shares from the holder to the vault pseudo-account (waiving transfer fees), then optionally `removeEmptyHolding` to clean up the holder's empty MPToken entry, and finally `accountSend` again to move the recovered assets from the vault pseudo-account to the issuer.

`removeEmptyHolding` is called only when the holder is not the vault owner. The vault owner's MPToken for shares is deliberately preserved because it anchors the share issuance — removing it would leave the MPTokenIssuance without an owner holding, which would be structurally inconsistent.

A negative-balance sanity check follows the asset transfer: `accountHolds` is called on the vault pseudo-account to confirm the vault's asset balance did not go negative. This is belt-and-suspenders validation against arithmetic bugs that would otherwise silently corrupt ledger state.

The method closes with `associateAsset(*vault, vaultAsset)`, the standard XRPL pattern for flushing `STNumber` rounding against the now-finalized vault SLE. Per the `STTakesAsset` contract, this must come after all mutations are complete, so that scaled numeric fields are rounded exactly once against their final committed values.