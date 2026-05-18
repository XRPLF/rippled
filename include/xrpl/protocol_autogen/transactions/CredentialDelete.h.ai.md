# `CredentialDelete.h` — Auto-Generated CredentialDelete Transaction Wrapper

## File Role and Context

This file is part of the `protocol_autogen` layer, a code-generated collection of type-safe C++ wrappers over XRPL's raw `STTx` serialized transaction objects. Every transaction type in the ledger gets its own generated header following an identical structural pattern: a read-only wrapper class paired with a fluent builder. `CredentialDelete.h` covers transaction type `ttCREDENTIAL_DELETE` (numeric code 60), which is gated behind the `featureCredentials` amendment and is used to remove on-ledger credential objects previously created by `CredentialCreate`.

The file lives in `include/xrpl/protocol_autogen/transactions/` alongside roughly 70 other per-transaction headers. It should never be edited by hand — its counterpart `CredentialCreate.h` and `CredentialAccept.h` are structurally identical products of the same generator.

## Two-Class Pattern: Wrapper and Builder

The header defines two classes inside `namespace xrpl::transactions`:

**`CredentialDelete`** is an immutable read-only wrapper. It holds a `std::shared_ptr<STTx const>` (inherited from `TransactionBase`) and exposes typed field getters with `[[nodiscard]]`. The constructor accepts a pre-existing `STTx` and immediately validates the transaction type, throwing `std::runtime_error` if the type does not match `ttCREDENTIAL_DELETE`. This fail-fast check prevents accidentally wrapping a payment or offer transaction with the wrong accessor class — a mistake the untyped `STTx::at()` API would silently permit.

**`CredentialDeleteBuilder`** is the construction entry point. It extends `TransactionBuilderBase<CredentialDeleteBuilder>` using CRTP so every common setter (`setFee`, `setSequence`, `setLastLedgerSequence`, etc.) returns `CredentialDeleteBuilder&` without needing casts in the caller. The builder accumulates fields into an `STObject object_{sfTransaction}` until `build(publicKey, secretKey)` is called, which invokes `sign()` from the base class (serializing the object with `HashPrefix::txSign`, computing the signature, and embedding it) before constructing a `CredentialDelete` wrapper from the resulting `STTx`.

A second builder constructor accepts an existing `std::shared_ptr<STTx const>` and copies the raw `STObject` fields into the mutable builder state. This supports a round-trip workflow: deserialize a network transaction, re-wrap it in the builder to modify optional fields, then re-sign and re-submit.

## Fields and Their Optionality

`CredentialDelete` has exactly three transaction-specific fields:

- **`sfCredentialType`** (`soeREQUIRED`, blob) — identifies which credential class to delete. This is the only mandatory field beyond the universal transaction fields (`sfAccount`, `sfSequence`, `sfFee`). Its getter returns `SF_VL::type::value_type` directly, not wrapped in `Optional`.

- **`sfSubject`** (`soeOPTIONAL`, account) — the account that holds the credential. Optional because either the issuer or the subject can initiate deletion. When the *issuer* sends this transaction, they specify `sfSubject` to target someone else's credential. When the *subject* sends it (deleting their own credential), the account is implicitly `sfAccount` and `sfSubject` is omitted.

- **`sfIssuer`** (`soeOPTIONAL`, account) — the account that originally issued the credential. Optional for the symmetric reason: when the *subject* sends this transaction, they specify `sfIssuer` to identify which issuer's credential to remove. When the *issuer* sends it, `sfIssuer` is omitted.

Contrast this with `CredentialCreate`, where `sfSubject` is `soeREQUIRED` — an issuer can only create credentials on behalf of others, so the subject must always be explicitly named. The optionality difference between the two transactions directly encodes the ledger's business rule that deletion is a bilateral privilege.

## The `protocol_autogen::Optional` Alias

Optional getters return `protocol_autogen::Optional<SF_ACCOUNT::type::value_type>` rather than a plain `std::optional`. The alias in `Utils.h` resolves to `std::optional<std::reference_wrapper<T>>` when `T` is a reference type, and to `std::optional<T>` otherwise. This avoids a subtle C++ pitfall: `std::optional<T&>` is ill-formed, so the template conditional ensures reference-typed fields are wrapped through `std::reference_wrapper` while value-typed fields use `std::optional` directly. `AccountID` is a value type, so both `getSubject()` and `getIssuer()` return `std::optional<AccountID>`.

## Inheritance and Validation

`TransactionBase` owns the shared pointer and provides all common read accessors (`getAccount()`, `getSequence()`, `getFee()`, `getFlags()`, `getMemos()`, the multi-sign `getSigners()`, and the newer `getDelegate()` for delegated transactions — consistent with the `Delegation::delegable` annotation on this transaction type). The `validate()` method in `TransactionBase` performs schema validation against the `TxFormats` singleton's `SOTemplate`, then runs `passesLocalChecks()` for non-pseudo transactions.

`TransactionBuilderBase` deliberately avoids calling `object_.set(soTemplate)` during initialization. Doing so would pre-populate `soeDEFAULT` placeholders that later cause `applyTemplate()` to reject "explicitly set to default" fields when the `STTx` constructor runs. Keeping `object_` as a free-form `STObject` and letting the `STTx` constructor call `applyTemplate()` itself is the correct sequencing — a non-obvious invariant that affects every builder in the autogen layer.