# `VaultClawback.h` — Vault Clawback Transactor

## Role in the System

`VaultClawback` implements the XRPL transaction that allows authorized parties to forcibly recover assets or shares held inside a `Vault` ledger object. Vaults are pooled asset containers backed by an MPT (Multi-Purpose Token) share issuance; depositors receive vault shares proportional to their contribution. `VaultClawback` is the enforcement mechanism that bridges two otherwise separate clawback regimes — IOU/MPT clawback by asset issuers, and vault-owner share cleanup — into a single vault-aware transaction type.

The class inherits from `Transactor` and follows the standard three-phase processing pipeline mandatory for all XRPL transactions: `preflight`, `preclaim`, and `doApply`. It declares `ConsequencesFactory{Normal}`, meaning it is not treated as a fee-blocker transaction.

---

## Two Operating Modes

The transaction operates in one of two fundamentally different modes, determined at execution time by comparing the submitting account against vault roles:

**1. Vault owner burning shares.** If the submitting account is the vault owner and the targeted amount refers to the share MPT, the vault owner can burn the holder's shares directly. This mode exists exclusively to recover from a stuck-vault scenario: shares remain outstanding but the vault holds zero assets (both `sfAssetsTotal` and `sfAssetsAvailable` are zero). This can happen if a lending protocol has absorbed all vault assets and suffered a total loss, leaving phantom shares with no underlying value. The owner may clawback all of a holder's shares in one operation, with no partial burns permitted (any non-zero amount must equal the holder's entire balance).

**2. Asset issuer clawing back vault assets.** If the submitter is the issuer of the vault's underlying asset (IOU or MPT), they can recover those assets from the vault's pseudo-account and proportionally destroy the holder's shares. This integrates with XRPL's pre-existing clawback capability: for IOU-backed vaults, the issuer account must carry `lsfAllowTrustLineClawback` and must not have set `lsfNoFreeze`; for MPT-backed vaults, the MPT issuance must carry `lsfMPTCanClawback`. XRP vaults are explicitly excluded from clawback at every validation stage, since XRP has no issuer.

---

## Validation Phases

### `preflight`

This phase runs before any ledger state is read, so checks are confined to transaction fields alone. It rejects a zero/empty `sfVaultID` immediately. If an explicit `sfAmount` is supplied, it must be non-negative and must not be XRP — since clawback of XRP is categorically forbidden.

### `preclaim`

This phase has read-only ledger access and resolves the ambiguity between modes. After loading the vault SLE and its share MPT issuance, `preclaim` resolves the effective clawback amount via the file-local helper `clawbackAmount()`, which converts an absent `sfAmount` field into either all-shares (if the submitter is the vault owner) or the vault asset (for the issuer case). 

A subtle edge case is handled explicitly: if the vault's asset issuer is the same account as the vault owner, the transaction is ambiguous — it cannot determine whether shares or assets should be targeted — so the submitter must provide an explicit `sfAmount`. This returns `tecWRONG_ASSET` rather than a more generic error, which is meaningful to clients.

For share-burn mode, `preclaim` enforces that the vault has shares outstanding but no assets whatsoever, preventing a vault owner from covertly extracting value via this path. The holder's share balance must exactly match the requested amount (or amount must be zero, meaning "all").

For asset-clawback mode, the permission check diverges by asset type: IOU trust lines consult the issuer's account flags, while MPT issuances consult their own issuance flags. This mirrors XRPL's existing `Clawback` transaction logic but is applied to the vault's pooled balance rather than a direct holder balance.

### `doApply`

Execution operates on the mutable ledger view. In share-burn mode, `sharesDestroyed` is set to the holder's full share balance; no assets move. In asset-clawback mode, the private `assetsToClawback()` helper computes the precise (shares, assets) pair to settle.

The sequence of mutations is:
1. Update vault `sfAssetsTotal` and `sfAssetsAvailable` downward.
2. Transfer `sharesDestroyed` shares from the holder to the vault's pseudo-account (waiving transfer fees).
3. Attempt to clean up the holder's now-empty MPToken entry via `removeEmptyHolding()`, tolerating `tecHAS_OBLIGATIONS` (meaning the token object has other uses).
4. Transfer `assetsRecovered` assets from the vault pseudo-account to the submitting issuer (waiving transfer fees), with a post-transfer sanity check that the vault's asset balance has not gone negative.

A zero `sharesDestroyed` at step 2 triggers `tecPRECISION_LOSS`, protecting against rounding-to-zero edge cases where the math would produce a no-op.

---

## `assetsToClawback` — Share/Asset Conversion with Safety Clamping

This private method is the mathematical core of asset-clawback mode. It uses `VaultHelpers.h`'s `assetsToSharesWithdraw` and `sharesToAssetsWithdraw` conversion functions, which compute the proportional shares/assets given the vault's current exchange rate.

The conversion is not simply the inverse of each other due to integer rounding (shares are MPTs and therefore integral). For this reason, `assetsToClawback` performs a double-pass when an explicit amount is provided: it first converts the requested asset amount to shares, then converts those shares back to assets to obtain the true recoverable amount. This round-trip accounts for integer truncation correctly.

When the computed `assetsRecovered` would exceed `sfAssetsAvailable` (possible in yield-bearing vaults where assets are partially deployed), the method clamps to `assetsAvailable` and recomputes shares with `TruncateShares::yes` — deliberately under-counting shares so the subsequent re-conversion of truncated shares back to assets cannot overshoot the cap. A second overflow check confirms the invariant held.

Arithmetic overflow from large amounts or unusual vault scales is caught via `std::overflow_error` and returned as `tecPATH_DRY`, a deliberate choice to log at `debug` rather than `error` since this is a normal user-reachable condition.

---

## Amendment Compatibility: `fixSecurity3_1_3`

A legacy code path in `assetsToClawback` is preserved for ledger replay. Before the `fixSecurity3_1_3` amendment, a zero-amount clawback (meaning "all") would convert all of the holder's shares to assets **without clamping to `sfAssetsAvailable`**, potentially allowing an issuer to recover more assets than the vault held liquid when an outstanding loan was in place. The fix adds the `assetsAvailable` clamp uniformly. The pre-fix branch is kept under an explicit rules check so that historical transaction replay produces the original (incorrect) ledger outcome.

---

## Relationship to Sibling Transactors

Among the vault transactors (`VaultCreate`, `VaultSet`, `VaultDelete`, `VaultDeposit`, `VaultWithdraw`), `VaultClawback` is structurally closest to `VaultWithdraw` — both convert shares to assets — but its authorization model is inverted: withdrawal is initiated by the holder themselves, while clawback is imposed on the holder by an external authority. This distinction explains why `VaultClawback` independently re-verifies asset-type clawback permissions that the issuer's own trust-line or MPT flags encode, rather than relying on any shared helper.