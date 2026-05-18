# `VaultCreate.cpp` — Vault Creation Transactor

## Role and Context

`VaultCreate.cpp` implements the `VaultCreate` transaction type, which allows an account on the XRP Ledger to instantiate a new on-chain vault. Vaults are yield-bearing, pooled asset structures that accept deposits from one or more holders, track their proportional ownership via MPT (Multi-Purpose Token) shares, and support optional permissioning through domains. This file is one of six vault transactors (alongside `VaultDeposit`, `VaultWithdraw`, `VaultSet`, `VaultDelete`, and `VaultClawback`) in the `src/libxrpl/tx/transactors/vault/` directory; `VaultCreate` handles only the initial construction of the vault ledger object and all of its supporting infrastructure.

## Three-Phase Validation Architecture

Like all XRPL transactors, `VaultCreate` separates validation into three stages: `checkExtraFeatures` (amendment gate), `preflight` (stateless field validation), and `preclaim` (stateful ledger checks), with `doApply` performing the actual mutations only if all three pass.

### `checkExtraFeatures`

This is a pure amendment gate. The vault feature set itself requires `featureMPTokensV1` — if that amendment is not active, the entire transaction type is rejected. A secondary gate protects the `sfDomainID` field: using it requires the `featurePermissionedDomains` amendment, allowing both features to activate independently without cross-coupling.

### `preflight` — Stateless Validation

`preflight` validates all transaction fields that can be checked without touching the ledger. Several decisions here are worth noting:

- **`sfWithdrawalPolicy`**: The only currently accepted value is `vaultStrategyFirstComeFirstServe` (constant `1`). This is future-proofing in design — the field exists in the protocol, but alternative strategies are not yet enabled; anything other than the one known value is rejected as `temMALFORMED`.

- **`sfDomainID` coupling**: A domain ID must be non-zero, and critically, it is only valid when the `tfVaultPrivate` flag is also set. Associating a permissioned domain with a public vault would be semantically incoherent — public vaults admit all holders — so this constraint is enforced at the earliest possible stage.

- **`sfScale` type restriction**: The scale factor (controlling IOU-to-share decimal precision) is meaningless for native XRP or MPT assets, which have fixed integer representations. Scale is rejected with `temMALFORMED` when the asset is `native()` or holds an `MPTIssue`, and is also bounded above by `vaultMaximumIOUScale` (18), chosen because 10^19 exceeds the maximum MPT amount (2^63 − 1 ≈ 10^18.9), ensuring that even a single IOU unit can always be converted to shares.

### `preclaim` — Stateful Ledger Checks

Three substantive checks occur here that require ledger access:

**`canAddHolding`**: Delegates to the asset-type-appropriate helper to verify that the vault's pseudo-account will be able to hold the given asset — checking MPT issuance limits and similar constraints.

**Pseudo-account issuer check**: Vaults whose asset is itself issued by a pseudo-account (e.g., shares of another vault, or AMM LP tokens) are rejected with `tecWRONG_ASSET`. The comment explains the reasoning precisely: pseudo-account-issued assets cannot be clawed back through the normal mechanism because the issuer has no private key and no direct authority path. Allowing such a vault to hold irrecoverable assets would be a permanent liability if the vault needed emergency intervention.

**Address collision check**: `pseudoAccountAddress(ctx.view, keylet::vault(account, sequence).key)` pre-computes the deterministic address that will be used for the vault's pseudo-account. If the result is `beast::zero`, the derived address collides with an existing account; the transaction returns `terADDRESS_COLLISION` before any state change occurs.

## `doApply` — Ledger Construction

The application phase creates four distinct on-chain objects and links them together. The ordering of operations is architecturally significant.

**1. Directory link and owner count**: `dirLink` inserts the new vault SLE into the owner's directory. Immediately after, `adjustOwnerCount` increments the owner's count by **2** — one for the vault `SLE` and one for the pseudo-account that will be created. This pre-increment happens before the reserve check so that the reserve calculation reflects the true post-creation state. The check `preFeeBalance_ < view().fees().accountReserve(ownerCount)` then uses the pre-fee XRP balance to confirm the owner can afford both new ledger objects simultaneously.

**2. Pseudo-account creation**: `createPseudoAccount(view(), vault->key(), sfVaultID)` creates a new `ACCOUNT_ROOT` SLE at the deterministically-derived address, with the vault's key stored in its `sfVaultID` field. This pseudo-account acts as the on-chain identity for the vault — it holds the vault's asset balance, issues vault shares, and is the entity counterparties interact with during deposits and withdrawals. It has no private key; it is controlled solely by the transactor logic.

**3. Empty holding for the asset**: `addEmptyHolding` establishes either an `MPToken` (for MPT assets) or a trust line / `RippleState` (for IOU assets) between the pseudo-account and the vault's underlying asset issuer. This zero-balance holding is necessary to initialize the account's relationship with the asset before any deposits arrive. XRP vaults do not need this call to create a holding, but the dispatch is handled uniformly.

**4. MPT share issuance creation**: `MPTokenIssuanceCreate::create` is called from the pseudo-account's perspective (with `sequence = 1`, since the pseudo-account was just created and has no prior issuances). This creates the `MPTokenIssuance` SLE that represents the vault's share tokens. The flags passed to the issuance are derived from the transaction's own flags:

- If `tfVaultShareNonTransferable` is **not** set, the shares receive `lsfMPTCanEscrow | lsfMPTCanTrade | lsfMPTCanTransfer`, making them freely tradeable on the DEX and via payment channels.
- If `tfVaultPrivate` is set, `lsfMPTRequireAuth` is added, restricting share transfers to explicitly authorized accounts.

Note the explicit comment: this is the issuance for *shares*, not for the asset itself — the comment exists because the call is structurally similar to the `addEmptyHolding` call above, and the distinction is easy to miss.

**5. Vault SLE population**: After all dependent objects exist, the vault's fields are populated: `sfAsset`, `sfOwner`, `sfAccount` (pseudo-account ID), `sfAssetsTotal`/`sfAssetsAvailable`/`sfLossUnrealized` (all starting at zero), optional `sfAssetsMaximum`, `sfShareMPTID` (the newly created issuance), `sfData`, and `sfWithdrawalPolicy` (defaulting to `vaultStrategyFirstComeFirstServe`). The private flag from the transaction is persisted directly into the vault's `sfFlags`.

**6. Owner MPToken authorization**: `authorizeMPToken` is called for the vault creator's account unconditionally, creating an `MPToken` SLE so the owner can hold vault shares from the moment the vault is created. For private vaults, a second `authorizeMPToken` call authorizes the pseudo-account itself — this is required so the pseudo-account can participate in share issuance mechanics, with the vault owner acting as the authorizing party.

**7. `associateAsset`**: The final call propagates the vault's asset type through all `sMD_NeedsAsset` fields in the vault SLE (primarily `STNumber` fields like `sfAssetsTotal`). This ties the asset's decimal scale to the number representation, ensuring serialization rounds correctly according to the asset's precision.

## Key Invariants

The reserve check is the only guard against the ledger being modified without sufficient XRP. If it fails after `adjustOwnerCount` has already incremented the owner count, the transaction is aborted as `tecINSUFFICIENT_RESERVE` and the ledger changes are rolled back by the framework — `doApply` returning a `tec` error code signals the framework to discard the apply view. The `// LCOV_EXCL_LINE` annotations on the internal error paths (`tefINTERNAL`, pseudo-account creation failure, share issuance failure) document that these are invariant violations that `preclaim` should have already made impossible under correct operation, serving as defensive assertions rather than reachable logic.