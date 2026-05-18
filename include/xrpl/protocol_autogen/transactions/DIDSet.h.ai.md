# `DIDSet.h` — Auto-Generated DIDSet Transaction Wrapper and Builder

This file is part of the `protocol_autogen` layer, a code-generated façade over the XRPL's core serialized transaction types. It provides two classes for the `DIDSet` transaction — `DIDSet` (an immutable, type-safe read wrapper) and `DIDSetBuilder` (a fluent construction interface) — following the same structural pattern used for every transaction type in the `include/xrpl/protocol_autogen/transactions/` directory.

## Protocol Context

`DIDSet` maps to transaction type `ttDID_SET` (type code 49), introduced under the `featureDID` amendment. It implements the W3C Decentralized Identifier (DID) standard on the XRP Ledger: an account submits a `DIDSet` transaction to create or update its on-ledger DID object, which can carry a raw DID document, a URI pointing to off-ledger DID metadata, or attestation data. Its counterpart, `DIDDelete` (type 50), removes the DID object entirely. The transaction is marked `delegable`, meaning another account can submit it on behalf of the DID owner when properly authorized, and requires no special privileges beyond a funded account.

## `DIDSet` — Immutable Wrapper

`DIDSet` inherits from `TransactionBase`, which wraps a `std::shared_ptr<STTx const>` and exposes read-only accessors for the common fields every transaction carries: `sfAccount`, `sfSequence`, `sfFee`, `sfFlags`, `sfMemos`, `sfSigners`, `sfDelegate`, and so on. `DIDSet` adds only the three DID-specific fields on top.

Construction is strict: the single-argument constructor takes a `shared_ptr<STTx const>` and immediately verifies that `tx_->getTxnType() == ttDID_SET`, throwing `std::runtime_error` on mismatch. This makes it impossible to accidentally wrap a wrong transaction type — the type-check is enforced at the C++ object boundary rather than relying on callers to inspect the transaction themselves.

The three transaction-specific fields are all optional (`soeOPTIONAL` in the XRPL schema) and all typed as `SF_VL` — variable-length blobs:

- `getDIDDocument()` / `hasDIDDocument()` — the raw W3C DID document bytes
- `getURI()` / `hasURI()` — a URI to off-ledger DID data
- `getData()` / `hasData()` — attestation or supplementary data

Every getter returns `protocol_autogen::Optional<SF_VL::type::value_type>`. The `Optional` alias, defined in `Utils.h`, uses `std::conditional_t` to produce either `std::optional<T>` or `std::optional<std::reference_wrapper<T>>` depending on whether `T` is a reference type. This indirection matters for `STArray`-backed fields in other transaction types where direct `std::optional` over a reference would be ill-formed; for the blob fields here it simply resolves to `std::optional<Blob>`. Each getter follows the same two-step pattern: call the `has*()` variant first, then delegate to `tx_->at(sf*)`. All getters are `[[nodiscard]]`, enforcing that callers actually use the returned value.

Since all three payload fields are optional, a `DIDSet` transaction is valid even when all three are absent — the ledger rules govern what constitutes a semantically meaningful combination, not the C++ type. A transaction that sets only `sfURI`, for instance, partially updates the existing DID object without touching the document bytes.

## `DIDSetBuilder` — Fluent Construction

`DIDSetBuilder` inherits from `TransactionBuilderBase<DIDSetBuilder>`, a CRTP template whose concrete type parameter enables each setter to return `Derived&` (i.e. `DIDSetBuilder&`) for method chaining without virtual dispatch. The base stores a mutable `STObject object_{sfTransaction}` and provides setters for all common fields. One subtle note in the base constructor: it deliberately avoids calling `object_.set(soTemplate)` so that fields left unset are absent rather than present with default sentinel values — the `STTx` constructor's own `applyTemplate()` call handles defaults correctly, but would reject explicitly-set defaults as invalid.

`DIDSetBuilder` offers two construction paths:

1. **Fresh construction**: takes `account` (required) plus optional `sequence` and `fee`. The transaction type is stamped into `object_[sfTransactionType]` immediately via the base constructor.
2. **Mutation of existing**: takes a `shared_ptr<STTx const>`, type-checks it, then copies the underlying `STObject` (`object_ = *tx`), allowing selective field updates before rebuilding.

The three `set*()` methods assign directly into `object_[sf*]`, returning `*this` for chaining. `build()` calls the protected `sign()` method — which serializes the object without signing fields, prepends `HashPrefix::txSign`, signs with the provided key pair, and writes `sfSigningPubKey` and `sfTxnSignature` back into `object_` — then constructs a `DIDSet` from a freshly heap-allocated `STTx`. Ownership of the signed transaction is transferred via `std::move(object_)`, leaving the builder in a moved-from state; `build()` should be called only once.

## Relationship to the Autogen Framework

This file is auto-generated and must not be edited manually. The pattern it instantiates is uniform across all ~70 transaction headers in the same directory: a `FooBar` read wrapper plus a `FooBarBuilder` fluent builder, with transaction-specific fields as the only variation between files. The shared logic lives in `TransactionBase` and `TransactionBuilderBase`, keeping the generated code minimal and consistent. The `DIDSet`/`DIDDelete` pair is among the simpler generated files because all DID-specific fields are optional blobs with no nested structures or complex field types.