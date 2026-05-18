# `TrustSet.h` — Auto-Generated TrustSet Transaction Wrapper

This file is part of the `protocol_autogen` layer, a code-generated collection of ~70 per-transaction headers under `include/xrpl/protocol_autogen/transactions/`. It provides two complementary classes for the `TrustSet` transaction type (`ttTRUST_SET`, numeric type 20): an immutable read-only wrapper for inspecting existing transactions and a fluent builder for constructing new ones. The file must not be hand-edited; any changes belong in the generator.

## Role in the XRPL Protocol

A `TrustSet` transaction creates or modifies a *trust line* between two accounts on the XRP Ledger. A trust line specifies the issuing account, the currency, and the maximum amount the submitting account is willing to hold of that issued currency (IOU). Without a trust line, an account cannot receive non-XRP assets. The three transaction-specific fields are all optional: `sfLimitAmount` encodes the currency, issuer, and credit limit as a single `STAmount`; `sfQualityIn` and `sfQualityOut` are legacy 32-bit fixed-point quality multipliers applied to incoming and outgoing transfers on the trust line.

## `TrustSet` — The Immutable Wrapper

`TrustSet` extends `TransactionBase`, which owns a `std::shared_ptr<STTx const>` — an already-signed, immutable serialized transaction object. The derived class adds three field-access pairs: a `hasX()` predicate and a `getX()` accessor returning `protocol_autogen::Optional<T>`.

The `Optional<T>` alias in `Utils.h` is non-trivial: it resolves to `std::optional<std::reference_wrapper<std::remove_reference_t<T>>>` when `T` is a reference type, and to `std::optional<T>` otherwise. This matters because `STTx::at()` on some field types returns a const reference into the object rather than a copy, and wrapping it in a raw `std::optional` would be a dangling-reference trap. The autogen layer handles this uniformly without callers needing to think about it.

The constructor takes a `std::shared_ptr<STTx const>` and immediately checks `getTxnType() != ttTRUST_SET`, throwing `std::runtime_error` on mismatch. This is a deliberate eager validation: a `TrustSet` instance can only ever wrap a `TrustSet` transaction, so no runtime guard is needed at each field access. The cost — one virtual dispatch at construction — is negligible compared to the safety guarantee.

`TransactionBase` also provides common-field accessors shared across all transaction types: `getAccount()`, `getSequence()`, `getFee()`, `getFlags()`, `getMemos()`, `getSigners()`, `getDelegate()`, and others. The `validate()` method cross-checks the wrapped `STTx` against the ledger's `TxFormats` schema template and runs local checks, giving a complete correctness guarantee beyond type safety alone.

## `TrustSetBuilder` — The Fluent Builder

`TrustSetBuilder` inherits from `TransactionBuilderBase<TrustSetBuilder>`, which uses CRTP so that the base-class setters (`setAccount()`, `setFee()`, `setSequence()`, `setFlags()`, etc.) return `Derived&` — a `TrustSetBuilder&` — preserving the concrete type through the chain. The internal state is a mutable `STObject object_{sfTransaction}`.

A key design note in `TransactionBuilderBase`: the constructor explicitly avoids calling `object_.set(soTemplate)`. Without this, `STTx`'s own `applyTemplate()` call during construction would encounter pre-populated placeholder fields for `soeDEFAULT` entries and throw "may not be explicitly set to default". The builder therefore keeps `object_` as a free `STObject`, relying on `STTx`'s constructor to enforce the schema on finalization.

`TrustSetBuilder` offers two construction paths: building fresh from account/sequence/fee, or reconstructing from an existing `STTx` (useful for editing a partially-built transaction). The second path copies the inner `STObject` via `object_ = *tx` and similarly validates the type upfront.

The `build()` method calls the protected `sign()` from `TransactionBuilderBase`, which serializes the object without signing fields, prepends `HashPrefix::txSign`, signs the payload with the provided `PublicKey`/`SecretKey`, attaches the signature, then constructs a final `STTx` — promoted from the mutable `STObject` — and wraps it in a `TrustSet` instance. After `build()`, the builder's `object_` is moved away and should not be reused.

The `setLimitAmount()`, `setQualityIn()`, and `setQualityOut()` setters take their values as `std::decay_t<typename SF_AMOUNT::type::value_type> const&` and `std::decay_t<typename SF_UINT32::type::value_type> const&`. Using `std::decay_t` strips any reference or cv-qualification from the field type's canonical form, which future-proofs these signatures against potential changes to the underlying `SField` type aliases without requiring regeneration of call sites.

## Autogen Pattern and Broader Context

Every transaction in this directory — from `Payment.h` to `VaultCreate.h` — follows the exact same dual-class pattern. The uniformity allows generic code consuming `TransactionBase*` to dispatch on `getTransactionType()` and then safely `static_cast` (or construct the appropriate typed wrapper) with confidence. The metadata in the `.ai.json` sidecar and the `// This file is auto-generated. Do not edit.` guard at the top signal that the generator is the single source of truth for field lists, optionality, and type mappings — any XRPL protocol amendment that adds or removes a `TrustSet` field is reflected here by regeneration, not manual edit.