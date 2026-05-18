# `VaultCreate.h` — Vault Creation Transactor

## Role in the System

`VaultCreate` is the transactor responsible for instantiating a new on-ledger Vault object in the XRPL. A Vault is a pooled-asset construct that allows an account to hold a designated asset inside a pseudo-account, while issuing share tokens (backed by an `MPTokenIssuance`) to depositors. `VaultCreate.h` declares the class interface; all logic lives in `VaultCreate.cpp`.

The file is one of six vault-lifecycle transactors (`VaultCreate`, `VaultSet`, `VaultDeposit`, `VaultWithdraw`, `VaultClawback`, `VaultDelete`), each of which inherits from `Transactor` and follows the same three-phase validation pattern used throughout the XRPL transaction engine.

## Class Design

`VaultCreate` publicly inherits from `Transactor` and contributes the standard four entry points the framework expects:

```cpp
static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};
```

The `Normal` consequence type tells the engine this transaction charges a regular fee and does not unconditionally block the account's sequence progression the way `Blocker` transactions do.

The constructor simply forwards its `ApplyContext` reference to `Transactor`, which stores it as `ctx_`. No additional state is needed at construction time because all inputs are read from `ctx_.tx` during the three phases.

## Validation Phases

### `checkExtraFeatures`

Unlike most transactors, `VaultCreate` overrides `checkExtraFeatures` (the base returns `true` by default). Here it gates the entire transaction on two amendments: the `MPTokensV1` feature must be enabled (vaults rely on MPT share issuance), and if the transaction carries a `sfDomainID` field, the `PermissionedDomains` feature must also be active. This design keeps amendment checks strictly out of `preflight` — the framework calls `checkExtraFeatures` before `preflight1`, so a disabled amendment yields `temDISABLED` before any field parsing occurs.

### `getFlagsMask`

Returns `tfVaultCreateMask`, which restricts the valid flag bits to those meaningful for vault creation (`tfVaultPrivate`, `tfVaultShareNonTransferable`). The base class framework passes this mask to `preflight0/preflight1`, which reject any unrecognised flag bits as `temINVALID_FLAG`.

### `preflight`

Performs stateless, ledger-free field validation:

- `sfData` payload length must not exceed `maxDataPayloadLength`.
- `sfWithdrawalPolicy`, if present, must equal `vaultStrategyFirstComeFirstServe` — the only currently supported strategy.
- `sfDomainID`, if present, must be non-zero and the `tfVaultPrivate` flag must be set (domain-restricted access only applies to private vaults).
- `sfAssetsMaximum` must not be negative.
- `sfMPTokenMetadata` must be non-empty and within `maxMPTokenMetadataLength`.
- `sfScale` is only valid for IOU assets — it is rejected for MPT or native XRP assets, and must not exceed `vaultMaximumIOUScale`.

### `preclaim`

Performs read-only ledger checks after signature verification:

- Calls `canAddHolding` to verify the asset can be held (e.g., asset exists, is not in a broken state).
- Rejects pseudo-account issuers (e.g., other vault share MPTs or AMM LP tokens) via `isPseudoAccount`. The comment explains the rationale: such assets would be impossible to claw back if ever needed.
- Checks that the asset is not frozen for the vault owner.
- If `sfDomainID` is present, confirms the referenced `PermissionedDomain` object exists on the ledger.
- Verifies that deriving a pseudo-account address from the vault's keylet does not produce a collision (`terADDRESS_COLLISION`).

## `doApply` — Ledger State Mutation

`doApply` performs the actual state changes inside a single atomic ledger view:

1. **Vault SLE creation**: A new `SLE` is built at `keylet::vault(account_, sequence)` and linked into the owner's directory via `dirLink`. The owner count is incremented by 2 (one for the vault object, one for the pseudo-account) before the reserve check — this is intentional: the reserve must be checked against the *post-creation* count to ensure the owner can afford both objects.

2. **Pseudo-account**: `createPseudoAccount` creates a synthetic account entry keyed from the vault's object ID. This pseudo-account holds the actual pooled asset on behalf of all depositors, keeping vault funds segregated from the owner's personal balance.

3. **Asset holding**: `addEmptyHolding` creates either an `MPToken` or a `TrustLine`/`RippleState` entry on the pseudo-account for the asset being vaulted — ready to receive deposits but initially empty.

4. **Share MPT issuance**: `MPTokenIssuanceCreate::create` is called on the pseudo-account (not the owner) at sequence 1, creating the share token that depositors receive in exchange for depositing the underlying asset. The `tfVaultShareNonTransferable` flag maps to clearing the `lsfMPTCanEscrow | lsfMPTCanTrade | lsfMPTCanTransfer` bits; `tfVaultPrivate` maps to setting `lsfMPTRequireAuth`.

5. **Vault SLE population**: All fields — `sfAsset`, `sfFlags`, `sfSequence`, `sfOwner`, `sfAccount` (pseudo-account ID), `sfAssetsTotal`, `sfAssetsAvailable`, `sfLossUnrealized`, optional `sfAssetsMaximum`, `sfShareMPTID`, `sfData`, `sfWithdrawalPolicy`, and `sfScale` — are written to the vault SLE before `view().insert` makes it permanent.

6. **Owner MPToken**: The vault creator is explicitly authorized for the share MPT via `authorizeMPToken`. For private vaults, the pseudo-account itself is also authorized (so it can receive its own share tokens internally), with the owner as the authorizing account.

## Notable Design Decisions

**Two-object owner reserve**: Incrementing the owner count by 2 before checking the reserve is a deliberate ordering choice. By checking the reserve after the increment, the code ensures the account genuinely has enough XRP to cover both the vault and pseudo-account entries simultaneously, preventing a scenario where creation succeeds only to leave the account underfunded.

**Pseudo-account isolation**: Routing all pooled funds through a pseudo-account (rather than a sub-balance on the owner's account) keeps vault assets cleanly separable from the owner's personal holdings and makes clawback semantics unambiguous. The explicit rejection of pseudo-account issuers in `preclaim` closes a circular-dependency risk where vault shares from one vault could be deposited into another.

**Compile-time polymorphism for static methods**: `checkExtraFeatures`, `getFlagsMask`, and `preflight` are all `static`. The base class comment in `Transactor.h` notes these methods use name hiding — not virtual dispatch — called through the `invokePreflight<T>` template to achieve polymorphism without vtable overhead. `doApply` is the sole `virtual` method, resolved at runtime.