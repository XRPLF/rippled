# `VaultSet.h` — Auto-Generated VaultSet Transaction Wrapper

## Role in the System

This file defines the `VaultSet` transaction for the XRPL Single-Asset Vault feature (`featureSingleAssetVault`). A vault on XRPL is an on-ledger yield-bearing pool that accepts a single asset type; `VaultSet` is the mutation transaction — it modifies properties of an already-existing vault without touching its asset holdings or ownership structure. The transaction type code is `ttVAULT_SET` (66).

The file is part of the `protocol_autogen` layer, a family of auto-generated headers that wrap XRPL's untyped `STTx` serialization objects behind strongly-typed, field-specific C++ APIs. Every vault lifecycle transaction (`VaultCreate`, `VaultDeposit`, `VaultWithdraw`, `VaultDelete`, `VaultClawback`) has a corresponding sibling file generated alongside this one. **The file must not be edited by hand** — the `// This file is auto-generated. Do not edit.` guard at the top signals that the source of truth lives in a separate generation template.

## Two-Class Pattern: Reader and Builder

The file follows the same two-class pattern shared across the entire `protocol_autogen` transaction family:

**`VaultSet`** is the read-only wrapper. It extends `TransactionBase` (which holds the `std::shared_ptr<STTx const> tx_` and provides common field accessors like `getAccount()`, `getFee()`, `getSequence()`). `VaultSet` adds transaction-specific accessors on top. Its constructor accepts a `shared_ptr<STTx const>` and immediately validates the embedded type tag — if the caller passes the wrong transaction type it throws `std::runtime_error` rather than silently presenting corrupted field reads later. Because `STTx const` is stored by `const` pointer, the wrapper is inherently immutable after construction.

**`VaultSetBuilder`** is the mutable construction half. It inherits from `TransactionBuilderBase<VaultSetBuilder>` using CRTP so that all common setter methods (`setFee()`, `setSequence()`, `setLastLedgerSequence()`, `setDelegate()`, etc.) return `VaultSetBuilder&` rather than a base reference, enabling clean method chaining without casts at call sites. The builder stores a live `STObject object_{sfTransaction}` (from the base class) and accumulates fields into it until `build()` is called.

The deliberate separation between reader and builder enforces a compile-time invariant: once a transaction is signed and wrapped in `VaultSet`, it cannot be mutated. Code that processes incoming transactions receives `VaultSet` and never inadvertently writes through it.

## Fields and Semantics

`VaultSet` carries one required field and three optional ones:

- **`sfVaultID`** (`SF_UINT256`, required) — the 256-bit identifier of the vault ledger object to modify. Because this is `soeREQUIRED`, it is injected by the builder's constructor: `VaultSetBuilder(account, vaultID, ...)` calls `setVaultID(vaultID)` immediately, so a builder can never exist in a state where `VaultID` is absent.

- **`sfAssetsMaximum`** (`SF_NUMBER`, optional) — a cap on the total assets the vault may hold. When omitted the vault is uncapped. Using `SF_NUMBER` (rather than `SF_AMOUNT`) allows the limit to be expressed as a raw numeric quantity independent of currency representation.

- **`sfDomainID`** (`SF_UINT256`, optional) — references a permissioned domain object, restricting who may interact with the vault. Setting or clearing this on an existing vault changes its access policy without redeploying the vault itself.

- **`sfData`** (`SF_VL`, optional) — an arbitrary variable-length blob for off-chain metadata. The ledger stores it verbatim; interpretation is left to applications.

For all three optional fields the pattern is a paired `has*()` / `get*()` on `VaultSet`. The getter returns `protocol_autogen::Optional<T>` (a thin alias for `std::optional<T>`) and guards behind `has*()` to avoid `STObject::at()` throwing on a missing field. This mirrors the XRPL ledger's `soeOPTIONAL` schema semantics in a way that the C++ type system can check statically.

## Access Constraints

The transaction comment block records two important policy annotations:

- `Delegation::notDelegable` — unlike some XRPL transactions, `VaultSet` cannot be submitted by a delegate account on behalf of the vault owner. Only the vault's controlling account may sign this transaction.
- Privilege `mustModifyVault` — the transaction processing layer enforces that the submitting account is the vault's owner before applying the modification.

## Build and Sign Flow

`VaultSetBuilder::build(publicKey, secretKey)` calls the inherited `sign()` from `TransactionBuilderBase`, which serializes the `STObject` with `HashPrefix::txSign` prepended, computes the cryptographic signature, and writes `sfSigningPubKey` and `sfTxnSignature` back into `object_`. It then constructs a `std::shared_ptr<STTx>` from the now-complete `STObject` (via `STTx(std::move(object_))`), wraps it in `VaultSet`, and returns by value. The `STTx` constructor calls `applyTemplate()` internally — which is precisely why `TransactionBuilderBase` avoids pre-initialising `object_` with the `soTemplate`; doing so would cause `applyTemplate()` to throw when it encounters `soeDEFAULT` placeholders that were already explicitly set.

A second builder constructor accepts an existing `shared_ptr<STTx const>` and copies the underlying object into `object_` via `object_ = *tx`. This path exists to re-open a signed transaction for modification, useful in testing or transaction relay scenarios where fields need adjustment before re-signing.

## Relationship to Sibling Transactions

`VaultCreate` (type 65, one below) is the creation counterpart. It requires `sfAsset` to specify what the vault holds and supports additional creation-time-only fields like `sfWithdrawalPolicy` and `sfScale`. `VaultSet` deliberately has no `sfAsset` field — the asset type of an existing vault is immutable by design. The subset of mutable vault properties (`sfAssetsMaximum`, `sfDomainID`, `sfData`) maps exactly to what `VaultSet` exposes.