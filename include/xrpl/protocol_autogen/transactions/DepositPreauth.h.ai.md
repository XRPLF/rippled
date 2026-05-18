# `DepositPreauth.h` — Auto-generated DepositPreauth Transaction Wrapper

## Role in the System

This file is part of the `protocol_autogen` layer — a code-generated family of per-transaction-type headers (roughly 70 in total) that provide C++ type-safe wrappers over XRPL's raw `STTx` serialization format. Each generated header follows an identical structural pattern: a read-only wrapper class and a paired builder class. `DepositPreauth.h` implements this pattern for transaction type `ttDEPOSIT_PREAUTH` (type code 19), which governs deposit pre-authorization on the XRP Ledger.

The DepositPreauth transaction allows an account that has enabled the `asfDepositAuth` flag to explicitly pre-authorize certain counterparties to send funds to it — bypassing the normal deposit authorization barrier. The transaction can authorize or revoke authorization for either a specific account (`sfAuthorize`/`sfUnauthorize`) or a set of credential-bearing accounts (`sfAuthorizeCredentials`/`sfUnauthorizeCredentials`), the latter reflecting the `Credentials` amendment. All four fields are optional, and a given submission uses exactly one of the four.

## `DepositPreauth` — Immutable Wrapper

`DepositPreauth` extends `TransactionBase`, which itself holds a `std::shared_ptr<STTx const>` as its protected `tx_` member. The `const` qualifier on `STTx` and the use of `shared_ptr` are load-bearing: multiple parts of the validation and processing pipeline can hold references to the same underlying transaction without any risk of mutation. The wrapper constructor immediately verifies `tx_->getTxnType() != txType` and throws `std::runtime_error` on mismatch, making it impossible to wrap a mistyped transaction at the C++ layer.

The four transaction-specific field accessors each follow the same two-method convention: a `has*()` predicate (`isFieldPresent`) and a `get*()` that returns an `Optional<T>` (or `std::nullopt` if absent). The `protocol_autogen::Optional<T>` alias defined in `Utils.h` is subtly important: for reference-typed fields it maps to `std::optional<std::reference_wrapper<std::remove_reference_t<T>>>`, while for value types it collapses to `std::optional<T>`. This lets the generated code use a single uniform return type regardless of whether the underlying field accessor returns by value or reference.

The `sfAuthorize` and `sfUnauthorize` accessors use `Optional<SF_ACCOUNT::type::value_type>` — a typed account ID — because those fields hold a single `AccountID`. The `sfAuthorizeCredentials` and `sfUnauthorizeCredentials` accessors take a different path: they return `std::optional<std::reference_wrapper<STArray const>>` and call `getFieldArray()` directly. This departure from the `Optional<T>` pattern reflects that `STArray` fields are structured composites (arrays of inner `STObject` entries representing credential type/issuer pairs) rather than scalar values with a compile-time type descriptor. The `[[nodiscard]]` attribute appears on every accessor, pushing callers to handle optional results explicitly rather than silently dropping them.

`TransactionBase` provides all the common field accessors shared across every transaction type: `getAccount()`, `getSequence()`, `getFee()`, `getFlags()`, `getMemos()`, `getSigners()`, `getDelegate()`, and several others. `DepositPreauth` adds only what is unique to this transaction type.

## `DepositPreauthBuilder` — Fluent Construction

`DepositPreauthBuilder` inherits from `TransactionBuilderBase<DepositPreauthBuilder>`, applying the Curiously Recurring Template Pattern (CRTP). The base class's setters all return `Derived&` via `static_cast<Derived&>(*this)`, making every method chain type-safe to the concrete builder rather than slicing back to the base. This allows fully-chained construction like:

```cpp
auto tx = DepositPreauthBuilder(account, seq, fee)
    .setLastLedgerSequence(1234)
    .setAuthorize(counterpartyId)
    .build(pubKey, secKey);
```

The builder holds an `STObject object_{sfTransaction}` (defined in `TransactionBuilderBase`) as mutable state. A deliberate design note in the base class comments explains why `object_.set(soTemplate)` is not called during construction: calling it would create `STBase` placeholders for `soeDEFAULT` fields, which would later cause `applyTemplate()` inside the `STTx` constructor to throw "may not be explicitly set to default." The builder deliberately keeps `object_` as a free object and relies on `STTx`'s constructor to apply the template correctly.

There are two builder constructors. The primary one takes an `account`, optional `sequence`, and optional `fee` — the minimal required fields for any valid transaction. The secondary one accepts an existing `std::shared_ptr<STTx const>` and copies its contents into `object_` via `object_ = *tx`, enabling round-trip editing of a deserialized transaction.

The `build()` method calls the protected `sign()` from `TransactionBuilderBase`, which serializes the transaction with `HashPrefix::txSign` prepended (as the XRPL signing protocol requires), computes the signature, sets `sfSigningPubKey` and `sfTxnSignature` on `object_`, then moves `object_` into a freshly constructed `STTx` wrapped in a `shared_ptr`. The resulting `STTx` is then handed to the `DepositPreauth` constructor, closing the loop between builder and wrapper.

## Design Observations

The split between immutable wrapper and mutable builder is a clean separation of concerns that matches the XRPL ledger's own model: transactions, once signed and submitted, are read-only records. Consumers of validated ledger data use `DepositPreauth`; code constructing new transactions uses `DepositPreauthBuilder`.

The transaction's `Delegation::delegable` annotation (visible in the class doc comment) means it can appear with an `sfDelegate` field identifying a different account acting on behalf of the signer. This field is accessed through `TransactionBase::getDelegate()`, not through anything in this file, reinforcing that the generated layer only adds transaction-specific fields on top of the common base.

Because the entire directory is auto-generated, modifying this file directly would be pointless — changes must be made upstream in whatever schema or template drives code generation. The consistent structure across all ~70 sibling files makes the generated layer straightforward for tooling and static analysis to process uniformly.