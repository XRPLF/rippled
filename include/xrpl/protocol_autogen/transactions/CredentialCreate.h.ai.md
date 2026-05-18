# `CredentialCreate.h` — Auto-generated Transaction Wrapper and Builder

## Role in the System

This header is part of the `protocol_autogen` layer, a code-generated abstraction over the raw `STTx` serialized-transaction format. It lives alongside similar per-transaction-type files — `CredentialAccept.h`, `CredentialDelete.h`, `Payment.h`, and roughly sixty others — all following the same two-class pattern: a read-only typed wrapper and a fluent builder.

`CredentialCreate` represents transaction type `ttCREDENTIAL_CREATE` (numeric value 58), introduced by the `featureCredentials` amendment. Within the XRP Ledger's Credentials feature, this transaction is how a credential issuer establishes a verifiable claim targeting a specific subject account. The lifecycle is three-part: an issuer submits `CredentialCreate` naming the subject and credential type, the subject responds with `CredentialAccept` (type 59), and either party can revoke via `CredentialDelete` (type 60). The file is explicitly marked `// This file is auto-generated. Do not edit.` — the source of truth is a code generator that reads the protocol's transaction schema and emits typed C++ for every transaction kind.

## Class Structure

### `CredentialCreate` — Immutable Wrapper

`CredentialCreate` inherits `TransactionBase` and wraps a `std::shared_ptr<STTx const>`. The `const` on `STTx` is load-bearing: once a transaction is promoted to this typed wrapper, its content cannot be mutated. The constructor enforces the type invariant immediately by comparing `tx_->getTxnType()` against the class-scoped `txType` constant (`ttCREDENTIAL_CREATE`) and throwing `std::runtime_error` on mismatch. This makes it impossible to accidentally call `getSubject()` on a `Payment` or any other transaction type — misidentification is caught at construction, not silently at runtime.

Field getters divide cleanly into two groups by the field's `soe` (serialized-object element) class:

**Required fields** (`soeREQUIRED`) — `getSubject()` and `getCredentialType()` — call `tx_->at(sf...)` directly and return by value. Required fields are always present in a well-formed transaction, so no existence check is needed.

**Optional fields** (`soeOPTIONAL`) — `getExpiration()` and `getURI()` — return `protocol_autogen::Optional<T>`, which is a type alias defined in `Utils.h`:

```cpp
template <typename ValueType>
using Optional = std::conditional_t<
    std::is_reference_v<ValueType>,
    std::optional<std::reference_wrapper<std::remove_reference_t<ValueType>>>,
    std::optional<ValueType>>;
```

The alias exists to handle the edge case where `ValueType` itself is a reference (which `std::optional` cannot hold directly). For the concrete types in this file — `uint32_t` for `sfExpiration` and `Blob` for `sfURI` — neither is a reference, so `Optional<T>` reduces to plain `std::optional<T>`. Each optional getter is paired with a `has*()` predicate (`hasExpiration()`, `hasURI()`) that probes `tx_->isFieldPresent()`. The getter checks the predicate before calling `at()`, avoiding the exception that `STTx::at` would throw for a missing field. All getters are marked `[[nodiscard]]` to prevent silently discarding return values, a common defensive pattern in the autogen layer.

### Field Schema

| Field | Type | Required | Description |
|---|---|---|---|
| `sfSubject` | `SF_ACCOUNT` | Yes | The account receiving the credential |
| `sfCredentialType` | `SF_VL` (blob) | Yes | Opaque identifier for the credential type |
| `sfExpiration` | `SF_UINT32` | No | Ledger-time expiration; absent means no expiry |
| `sfURI` | `SF_VL` (blob) | No | External URI pointing to credential metadata |

### `CredentialCreateBuilder` — Fluent Transaction Constructor

`CredentialCreateBuilder` inherits the CRTP base `TransactionBuilderBase<CredentialCreateBuilder>`. The CRTP ensures that setters inherited from the base (for `sfAccount`, `sfFee`, `sfSequence`, `sfLastLedgerSequence`, delegation fields, etc.) return `CredentialCreateBuilder&`, not a base-class reference, keeping the fluent chain unbroken without any casts at the call site.

The builder maintains an `STObject object_{sfTransaction}` member (inherited from the base) that accumulates field assignments. Notably, the base constructor avoids calling `object_.set(soTemplate)` — a deliberate choice documented in the base: calling `applyTemplate()` early would create `STBase` placeholders for `soeDEFAULT` fields, which would then cause the `STTx` constructor to throw "may not be explicitly set to default" because those placeholders look like intentional assignments of default values.

The primary constructor enforces required-field completeness: `sfSubject` and `sfCredentialType` must be provided upfront; `sfExpiration` and `sfURI` are set separately as optional. A secondary constructor accepts a `shared_ptr<STTx const>` and copies the existing serialized object into `object_`, enabling re-signing or field modification of a pre-existing raw transaction. This secondary path validates the transaction type the same way the wrapper does.

`build(PublicKey, SecretKey)` finalizes the transaction: it calls the protected `sign()` method from the base, which serializes the `STObject` excluding signing fields, prepends `HashPrefix::txSign`, computes the signature with the provided key pair, and sets `sfSigningPubKey` and `sfTxnSignature`. It then constructs the `CredentialCreate` wrapper from `std::move(object_)`, meaning the builder's internal `STObject` is consumed — calling `build()` a second time would produce a wrapper over an empty object.

## Delegation Support

The transaction is marked `Delegation::delegable`, which means the common field `sfDelegate` (exposed via `TransactionBase::getDelegate()`) may be set to allow a third-party account to submit the transaction on behalf of the issuer. This is handled entirely by the base layer; `CredentialCreate` itself adds no delegation-specific logic.

## Relationship to Sibling Files

`CredentialCreate.h`, `CredentialAccept.h`, and `CredentialDelete.h` form the autogenerated face of the Credentials feature. The transactor logic that validates and applies `CredentialCreate` on the ledger lives in `src/libxrpl/tx/transactors/credentials/CredentialCreate.cpp` and `include/xrpl/tx/transactors/credentials/CredentialCreate.h` — those files operate on raw `STTx` references and contain the actual invariant enforcement (duplicate credential checks, subject account existence, etc.). The autogenerated wrappers here are used in test harnesses and application code that needs to construct or inspect transactions without manipulating the serialized format directly.