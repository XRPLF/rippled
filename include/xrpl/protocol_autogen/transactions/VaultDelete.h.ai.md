# `VaultDelete.h` — Auto-generated VaultDelete Transaction Type

## Role and Context

This header defines the C++ representation of the `VaultDelete` transaction for the XRP Ledger's Single Asset Vault feature, introduced under the `featureSingleAssetVault` amendment. A vault is an on-ledger pool that holds a single asset and issues MPT (Multi-Purpose Token) vault shares to depositors. `VaultDelete` is the teardown operation: it destroys the vault, eliminates the associated MPT issuance that backed the shares, and optionally deletes the vault's pseudo-account — making it the most destructive transaction in the vault lifecycle family alongside `VaultCreate` (type 65), `VaultSet`, `VaultDeposit`, `VaultWithdraw`, and `VaultClawback`.

The file is machine-generated and must not be edited by hand. Every transaction type in the `protocol_autogen/transactions/` directory follows the same two-class pattern: an immutable read-only wrapper and a corresponding builder. This consistency is enforced structurally rather than by convention, since all wrappers derive from `TransactionBase` and all builders derive from `TransactionBuilderBase<Derived>`.

## `VaultDelete` — Immutable Transaction Wrapper

`VaultDelete` is a thin, read-only view over a `shared_ptr<STTx const>`. It inherits the full suite of common field accessors from `TransactionBase` — account, sequence, fee, flags, memos, signers, delegate, network ID, etc. — and adds exactly one transaction-specific accessor: `getVaultID()`, which returns the `sfVaultID` field as a `uint256` value.

The minimalism of `VaultDelete` compared to `VaultCreate` (which carries six fields including asset type, withdrawal policy, metadata, and cap) is intentional: deletion only needs to identify *which* vault to remove. The vault ID is declared `soeREQUIRED`, so `getVaultID()` always returns a valid value without an optional wrapper.

The constructor enforces a runtime type check against the static constant `txType = ttVAULT_DELETE`. This guard is necessary because `STTx` is a generic container: constructing a wrapper around the wrong transaction type would compile cleanly but silently return meaningless values from field accessors. Since these wrappers are often built from deserialized network objects, catching the mismatch at construction time is the earliest safe point.

The transaction carries the privilege flags `mustDeleteAcct | destroyMPTIssuance | mustModifyVault`, reflecting its side effects on three different ledger object categories. It is marked `Delegation::notDelegable`, meaning it cannot be submitted on behalf of another account via the delegate mechanism exposed in `TransactionBase::getDelegate()`.

## `VaultDeleteBuilder` — Fluent Builder

`VaultDeleteBuilder` extends `TransactionBuilderBase<VaultDeleteBuilder>` via the Curiously Recurring Template Pattern (CRTP). The base class provides all common field setters (`setAccount`, `setFee`, `setSequence`, `setFlags`, `setMemos`, `setDelegate`, etc.) which return `Derived&` to enable method chaining, while the derived builder adds only `setVaultID()`.

The primary constructor accepts an account, the vault ID, and optionally a sequence number and fee. The vault ID is taken as `std::decay_t<typename SF_UINT256::type::value_type> const&`, which strips reference qualifiers from the serialized field's underlying type — a consistent pattern across all generated builders that prevents accidental dangling references to temporary values.

The secondary constructor accepts an existing `shared_ptr<STTx const>` and copies the transaction body into the builder's internal `STObject object_`. This copy-from-transaction path allows re-inflating a signed transaction into a mutable builder for inspection or re-signing, though it naturally drops the original signature. The type check here mirrors the one in the wrapper constructor.

`build()` finalizes the transaction in two steps: it calls the inherited `sign()` method, which computes the canonical signing bytes as `HashPrefix::txSign` concatenated with the serialized object (excluding signing fields), then moves the completed `STObject` into an `STTx` via `std::make_shared<STTx>(std::move(object_))`. The move means the builder's internal state is consumed: calling `build()` twice on the same builder without re-populating fields would produce a broken transaction.

## Design Tradeoffs

The split between an immutable wrapper type and a separate builder type is a deliberate separation of concerns. The wrapper's `const`-correctness guarantees that code receiving a `VaultDelete` reference cannot mutate it, which matters when the same `STTx` is shared across multiple processing stages in the ledger engine. Constructing a fresh `STTx` inside `build()` — rather than returning a mutable wrapper — keeps the immutability invariant intact even if the caller retains the builder.

The `TransactionBuilderBase` deliberately avoids calling `object_.set(soTemplate)` on construction (as noted in its inline comment). Setting the SOTemplate on a free `STObject` would pre-populate all optional fields with their defaults, causing `STTx::applyTemplate()` to throw "may not be explicitly set to default." By leaving the object as a free container and letting `STTx`'s constructor apply the template during finalization, the builder avoids this pitfall without any per-field special-casing.