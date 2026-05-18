# `AMMDelete.h` — Auto-generated AMMDelete Transaction Wrapper

## Role in the System

`AMMDelete.h` is an auto-generated header in the `xrpl::transactions` namespace that provides two tightly coupled classes for working with the `AMMDelete` on-ledger transaction type (`ttAMM_DELETE`, type code 40). It lives in the `protocol_autogen/transactions/` layer, one of roughly 70 similar per-transaction headers that collectively expose the XRPL transaction set as first-class C++ types rather than loosely typed `STObject` bags.

The transaction it wraps was introduced by the `featureAMM` amendment and serves a specific lifecycle role: it removes an Automated Market Maker (AMM) instance from the ledger, but *only* when the AMM's pool is already empty — no assets remain and no LP tokens are in circulation. The two required fields, `sfAsset` and `sfAsset2`, identify the asset pair that defined the AMM pool being deleted. The privilege flags `mustDeleteAcct | mayDeleteMPT` indicate that this operation tears down the AMM's pseudo-account and may also delete any related Multi-Purpose Token (MPT) structures.

## Two-Class Design: Wrapper and Builder

The file exposes two classes with complementary responsibilities:

**`AMMDelete`** is a read-only, immutable wrapper around a `std::shared_ptr<STTx const>`. It inherits the full suite of common field getters from `TransactionBase` (account, sequence, fee, flags, signers, etc.) and adds only the two transaction-specific accessors: `getAsset()` and `getAsset2()`, both returning `SF_ISSUE::type::value_type`. The constructor verifies that the wrapped `STTx` actually carries `ttAMM_DELETE` and throws `std::runtime_error` otherwise — a hard invariant that prevents type confusion when routing deserialized transactions.

**`AMMDeleteBuilder`** is the mutable counterpart, inheriting from `TransactionBuilderBase<AMMDeleteBuilder>` via CRTP. This template pattern lets the base class return `Derived&` from every setter, enabling fluent method chaining without any virtual dispatch overhead. The builder accumulates fields into an `STObject object_{sfTransaction}` member and intentionally *avoids* calling `object_.set(soTemplate)`. The base class comment explains the reason: calling `applyTemplate()` on a free `STObject` would create `STBase` placeholders for `soeDEFAULT` fields, and those placeholders later cause `STTx`'s constructor to throw "may not be explicitly set to default." The design trusts `STTx`'s own `applyTemplate()` call to handle unset optional fields correctly at build time.

The constructor requires `account`, `asset`, and `asset2` up front — matching the protocol specification where both fields carry `soeREQUIRED` — while `sequence` and `fee` are `std::optional` to accommodate scenarios like autofill or ticket-based submission.

A secondary constructor accepts an existing `std::shared_ptr<STTx const>` and copies the underlying `STObject` into `object_`, enabling round-tripping: deserialize a transaction, wrap it in a builder, modify fields, then re-sign and re-build.

## Build and Sign Flow

`build(PublicKey const&, SecretKey const&)` finalises construction. It delegates to `sign()` in the base class, which sets `sfSigningPubKey` from the public key's slice, serialises the `STObject` with `addWithoutSigningFields()` prefixed by `HashPrefix::txSign`, signs the digest, and stores the resulting signature in `sfTxnSignature`. It then wraps the `STObject` in a new `STTx` (taking ownership via move) and passes that `shared_ptr` to `AMMDelete`'s constructor. The transition from builder (mutable `STObject`) to wrapper (immutable `shared_ptr<STTx const>`) is one-way and explicit — once built, the transaction cannot be further mutated without constructing a new builder.

## MPT Support

Both `setAsset`/`setAsset2` store values as `STIssue` objects (`STIssue(sfAsset, value)`). The `soeMPTSupported` annotation in the transaction macro (in `transactions.macro`) confirms that both asset fields accept either classic IOU issues or Multi-Purpose Token identifiers. This is relevant because AMM pools on the XRPL can be formed from MPT/MPT or MPT/IOU pairs under the extended AMM feature set, and `AMMDelete` must be capable of referencing either kind.

## Auto-generation and Maintenance

The file header declares `// This file is auto-generated. Do not edit.` The canonical source of truth is the `TRANSACTION(ttAMM_DELETE, 40, ...)` macro expansion in `protocol/detail/transactions.macro`, which lists the field schema. Any addition of optional fields to the `AMMDelete` schema would regenerate this file with corresponding `getField()`/`setField()` pairs, keeping the typed wrapper in sync without manual effort. This pattern is consistent across all ~70 transaction types in the directory, making the autogen layer a reliable, uniform boundary between the protocol schema definition and downstream C++ consumers.