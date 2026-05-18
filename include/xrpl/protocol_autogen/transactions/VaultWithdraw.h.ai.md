# `VaultWithdraw.h` — Auto-Generated VaultWithdraw Transaction Wrapper

This file is machine-generated (the `// This file is auto-generated. Do not edit.` header is definitive) and lives in `include/xrpl/protocol_autogen/transactions/`. It defines two closely related classes — `VaultWithdraw` and `VaultWithdrawBuilder` — that together implement the full lifecycle of a `ttVAULT_WITHDRAW` (type 69) transaction for the XRPL Single-Asset Vault feature (`featureSingleAssetVault`). Its role is to translate the loosely-typed, schema-driven `STTx` layer into compile-time-checked C++ types, shielding application code from raw field manipulation.

## Context: the Single-Asset Vault system

`VaultWithdraw` is one of six vault transaction types (`VaultCreate`, `VaultDeposit`, `VaultWithdraw`, `VaultSet`, `VaultDelete`, `VaultClawback`), all sharing the same generated structure. A vault in this sense is an on-ledger pooled-asset construct; depositors put assets in (via `VaultDeposit`, type 68) and withdraw them (via `VaultWithdraw`, type 69). The amount field deliberately supports MPT (Multi-Purpose Token) quantities in addition to classic XRP/IOU `STAmount`, which is why getters and setters annotate it with `@note This field supports MPT amounts`.

Compared to `VaultDeposit`, which only exposes `sfVaultID` and `sfAmount`, `VaultWithdraw` adds two optional fields — `sfDestination` and `sfDestinationTag` — allowing the withdrawal proceeds to be sent to a *different* account than the transaction sender. This mirrors the Payment transaction pattern and is the key structural difference between the deposit and withdrawal sides of the vault.

The transaction carries the privilege flags `mayDeleteMPT | mayAuthorizeMPT | mustModifyVault` and is marked `Delegation::notDelegable`, meaning a delegated account cannot submit this on behalf of the vault owner.

## Two-class design: wrapper + builder

The split between an immutable read wrapper and a mutable builder is intentional and consistent across all generated transaction types. `VaultWithdraw` itself is constructed only from an existing `std::shared_ptr<STTx const>` — the `const`-qualified `STTx` makes mutation impossible after wrapping. The constructor immediately verifies the transaction type tag and throws `std::runtime_error` on mismatch, preventing a caller from accidentally treating a `VaultDeposit` as a `VaultWithdraw`. All getter methods are `[[nodiscard]]` to catch ignored return values at compile time.

`VaultWithdrawBuilder` extends the CRTP base `TransactionBuilderBase<VaultWithdrawBuilder>`, which supplies setters for every universal transaction field (`sfAccount`, `sfFee`, `sfSequence`, `sfMemos`, `sfSigners`, etc.) while returning `Derived&` to keep the fluent chain typed correctly. The builder holds a mutable `STObject object_` rather than an `STTx`, deliberately avoiding `set(soTemplate)` on it — the comment in `TransactionBuilderBase` explains that calling `applyTemplate()` too early would create `STBase` placeholders for `soeDEFAULT` fields, which then cause `applyTemplate()` to throw "may not be explicitly set to default" when the `STTx` is finally constructed. Only the terminal `build()` call promotes the `STObject` into an `STTx`.

The builder provides two constructors: the primary one takes the required fields (`account`, `vaultID`, `amount`) and optional `sequence`/`fee`, wiring them through the CRTP base and the vault-specific setters. The second constructor accepts an existing `std::shared_ptr<STTx const>` for round-trip editing — it copies the raw `STTx` back into `object_` via `object_ = *tx`, again guarded by a type-check throw. This round-trip path exists so that a transaction already present in the ledger can be re-signed or modified without starting from scratch.

## Field handling pattern for optional fields

`getDestination()` and `getDestinationTag()` follow a paired `hasX()` / `getX()` pattern that wraps the optional check explicitly rather than using `STTx::at()` with a default. Each getter returns `protocol_autogen::Optional<T>` (a thin alias for `std::optional<T>`), and the `has*()` helpers delegate to `STTx::isFieldPresent()`. Required fields (`sfVaultID`, `sfAmount`) skip the presence check entirely and call `tx_->at()` directly, which will throw on a missing field — a deliberate fail-hard choice since a required field's absence indicates a malformed transaction that already should have been rejected by schema validation in `TransactionBase::validate()`.

## Build and sign terminal operation

`VaultWithdrawBuilder::build(PublicKey, SecretKey)` is the only exit point for creating a signed transaction. It calls the protected `sign()` method from `TransactionBuilderBase`, which serialises the `STObject` with `HashPrefix::txSign` prepended and without signing fields (per XRPL's canonical signing scheme), then appends the resulting signature as `sfTxnSignature`. The fully signed `STObject` is moved into a fresh `STTx`, which is then wrapped in a `VaultWithdraw` and returned by value. After `build()` returns, the builder's internal `object_` has been moved-from, so the builder should not be reused.