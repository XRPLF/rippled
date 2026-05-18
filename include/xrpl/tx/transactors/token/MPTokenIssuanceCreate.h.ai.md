# `MPTokenIssuanceCreate.h` — MPT Issuance Creation Transactor

## Role in the System

This header defines the transactor class and its parameter aggregate for creating a new Multi-Party Token (MPT) issuance on the XRP Ledger. MPTs are a newer token primitive that carries significantly more metadata than classic trust-line IOUs: optional supply caps, decimal scaling, configurable transfer fees, arbitrary binary metadata, and permissioned-domain constraints. `MPTokenIssuanceCreate` is the entry point for producing the `MPTokenIssuance` ledger object that anchors all subsequent holder balances.

## `MPTCreateArgs` — A Shared Parameter Struct

The struct exists specifically to decouple the creation logic from the transaction-processing context. Rather than having `doApply()` call internal helpers that read directly from `ctx_.tx`, all inputs are first gathered into an `MPTCreateArgs` value and then forwarded to the static `create()` method. This lets other transactors that need to mint an MPT issuance as a side effect — most notably `VaultCreate`, which mints a share-token issuance when creating a single-asset vault — reuse the same creation path without going through a full transaction flow.

Notable fields:

- `priorBalance` — optional `XRPAmount` representing the account's balance before fee deduction. When present, `create()` uses it to enforce the reserve requirement (`tecINSUFFICIENT_RESERVE`). When absent (e.g., during vault share creation, where the pseudo-account's reserve is managed separately), the check is skipped.
- `sequence` — combined with `account` to derive the deterministic `MPTID` via `makeMptID()`.
- `flags` / `mutableFlags` — the transaction-level flags are stored stripped of universal bits (`~tfUniversal`) into the `sfFlags` field of the issuance SLE. The optional `mutableFlags` field (gated behind `featureDynamicMPT`) records which of those flags can be changed after issuance.
- `domainId` — links the issuance to a permissioned domain; requires both the `featurePermissionedDomains` and `featureSingleAssetVault` amendments.

## `MPTokenIssuanceCreate` — Transactor Lifecycle

The class follows the standard XRPL transactor pattern by inheriting `Transactor` and implementing three static preflight hooks and the `doApply()` virtual method:

**`checkExtraFeatures()`** gates two optional field categories at the amendment level. If `sfDomainID` appears in the transaction but neither `featurePermissionedDomains` nor `featureSingleAssetVault` is enabled, the method returns `false`, which `invokePreflight` translates to `temDISABLED`. Similarly, `sfMutableFlags` requires `featureDynamicMPT`. This pattern keeps amendment checks entirely separate from semantic validation in `preflight()`.

**`getFlagsMask()`** returns `tfMPTokenIssuanceCreateMask`, the bitmask of all flags legal for this transaction type. `invokePreflight()` passes this to `preflight1()`, which rejects any unknown flag bits before reaching the transaction's own `preflight()`.

**`preflight()`** enforces four semantic constraints:

1. If `sfMutableFlags` is present it must be non-zero and must not contain bits outside the mutable-flags mask — preventing zero-effect or invalid update grants.
2. A non-zero `sfTransferFee` (max `maxTransferFee` = 50,000 basis points) is only meaningful when `tfMPTCanTransfer` is also set; inconsistency returns `temMALFORMED`.
3. A `sfDomainID` of zero is malformed, and a non-zero domain ID implies the issuance is not public — `tfMPTRequireAuth` must be set, otherwise holders outside the permissioned domain could not legally be authorized.
4. `sfMPTokenMetadata` must be non-empty and at most `maxMPTokenMetadataLength` (1024) bytes. `sfMaximumAmount`, if provided, must be positive and within the unsigned 63-bit ceiling `maxMPTokenAmount` (0x7FFF_FFFF_FFFF_FFFF).

## The Static `create()` Method and Its Return Type

```cpp
static Expected<MPTID, TER>
create(ApplyView& view, beast::Journal journal, MPTCreateArgs const& args);
```

Returning `Expected<MPTID, TER>` rather than plain `TER` is deliberate: success carries a usable value (the new issuance ID) rather than requiring the caller to recompute it. The caller wraps failure with `Unexpected(tecINTERNAL)`, `Unexpected(tecINSUFFICIENT_RESERVE)`, or `Unexpected(tecDIR_FULL)`.

Inside `create()`, the `MPTID` is deterministically computed as the keccak/sha512-half of `(sequence, account)` via `makeMptID()`, which is itself a 192-bit `base_uint`. The function then:

1. Checks the account's XRP reserve if `priorBalance` is set.
2. Inserts an entry into the issuer's owner directory via `view.dirInsert()` — failing with `tecDIR_FULL` if the directory page is exhausted.
3. Constructs the `MPTokenIssuance` SLE with mandatory fields (`sfFlags`, `sfIssuer`, `sfOutstandingAmount = 0`, `sfOwnerNode`, `sfSequence`) and conditionally populates all optional fields.
4. Increments the account's `sfOwnerCount` through `adjustOwnerCount()`, which affects future reserve calculations.

`doApply()` simply packages `ctx_.tx` fields into `MPTCreateArgs` and delegates to `create()`, collapsing the `Expected` result to `tesSUCCESS` or its embedded error. This thin wrapper is the reason `create()` takes an `ApplyView&` and `beast::Journal` directly — it needs no `ApplyContext` beyond those two, which keeps `VaultCreate` from needing to fabricate a full context.

## Design Tradeoffs

The separation of `MPTCreateArgs` and the static `create()` method is a deliberate composability pattern seen throughout the vault transactor family. It avoids copy-pasting ledger-mutation logic and means that any future transactor that needs to programmatically issue an MPT can do so with the same safety guarantees as the user-facing transaction, including reserve enforcement and directory insertion, simply by choosing whether to pass `priorBalance` or not.