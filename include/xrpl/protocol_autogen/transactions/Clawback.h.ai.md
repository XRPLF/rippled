# `Clawback.h` — Auto-Generated Clawback Transaction Wrapper

## Role and Context

This file is part of the `protocol_autogen` subsystem, a layer of auto-generated, type-safe C++ wrappers over XRPL's raw `STTx` serialization objects. It defines two classes — `Clawback` and `ClawbackBuilder` — representing the `ttCLAWBACK` (type 30) transaction, which allows token issuers to reclaim previously distributed tokens from holder accounts. The transaction is gated behind the `featureClawback` amendment and is marked delegable, meaning it can be authorized to a delegate account via `sfDelegate`.

The file's header comment explicitly states it is machine-generated and must not be edited by hand. In practice it is one of ~70 transaction-specific files in the same `transactions/` directory, each following the same structural pattern: a read-only wrapper class paired with a corresponding builder.

## The `Clawback` Read-Only Wrapper

`Clawback` inherits from `TransactionBase`, which holds an `std::shared_ptr<STTx const>` — a const-qualified, reference-counted pointer to the underlying serialized transaction object. The `const` qualifier is the key design decision: it enforces that once a transaction is created and signed, it is immutable. No mutation is possible through this class.

Construction validates the transaction type immediately:

```cpp
if (tx_->getTxnType() != txType)
    throw std::runtime_error("Invalid transaction type for Clawback");
```

This guard prevents accidental wrapping of a `Payment` or `TrustSet` in a `Clawback` shell, catching type mismatches at the earliest possible point rather than letting them silently corrupt field reads.

The class exposes two transaction-specific fields beyond what `TransactionBase` provides:

**`getAmount()`** returns the `sfAmount` field, which is `soeREQUIRED` — it always exists and the getter never returns an `optional`. Notably, the documentation marks it as supporting MPT (Multi-Purpose Token) amounts, meaning the field can hold either a traditional IOU `STAmount` or an MPT quantity. This is important because `Clawback` was extended to cover MPT when that feature was introduced.

**`getHolder()` / `hasHolder()`** handle the optional `sfHolder` field, which carries an `AccountID` identifying the account whose tokens are being reclaimed. For IOU clawbacks the holder relationship is implicit in the amount's currency/issuer context, but for MPT clawback an explicit holder account must be named. The pair pattern — a `get` that returns `protocol_autogen::Optional<T>` (an alias for `std::optional<T>`) and a `has` predicate — is the standard idiom used throughout the autogen layer for all optional fields.

## The `ClawbackBuilder` Class

`ClawbackBuilder` inherits from `TransactionBuilderBase<ClawbackBuilder>`, a CRTP base that owns a mutable `STObject object_{sfTransaction}` and provides fluent setters for the universal transaction fields (`sfAccount`, `sfFee`, `sfSequence`, `sfLastLedgerSequence`, `sfMemos`, `sfDelegate`, etc.). Each setter in the base returns `Derived&`, making method chains resolve to `ClawbackBuilder&` without any casts at the call site.

The constructor accepts `sfAmount` as required and `sfSequence`/`sfFee` as optionals:

```cpp
ClawbackBuilder(SF_ACCOUNT::type::value_type account,
                std::decay_t<typename SF_AMOUNT::type::value_type> const& amount,
                std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt)
```

The `std::decay_t` wrapper strips references and cv-qualifiers from `SF_AMOUNT::type::value_type` before rebinding it as a `const&`. This is a defensive pattern to avoid binding a `const reference` to a reference type, which would be ill-formed.

The base class constructor deliberately avoids calling `object_.set(soTemplate)`. This is explained in an inline comment in `TransactionBuilderBase`: setting the template would insert `STBase` placeholders for `soeDEFAULT` fields, and when the `STTx` constructor later calls `applyTemplate()`, those placeholders would trigger an exception ("may not be explicitly set to default"). By keeping `object_` as a free, unconstrained object during construction, fields are set only when explicitly provided.

A second constructor accepts an existing `std::shared_ptr<STTx const>` and copies its contents into `object_`, enabling a round-trip workflow where a received transaction can be deserialized back into builder form for modification before re-signing.

The `build()` method finalizes construction: it delegates to `TransactionBuilderBase::sign()`, which serializes the object with `HashPrefix::txSign` prepended (per the XRPL signing specification) and computes the `sfTxnSignature`, then wraps the result in `std::make_shared<STTx>` and returns a `Clawback` wrapper. After `build()` is called, the immutability guarantee of `Clawback` applies — there is no way to modify the signed transaction through the returned object.

## Architectural Position

`Clawback.h` is purely an interface file; no implementation logic beyond field access and type checking lives here. The actual ledger enforcement of clawback rules — validating that the issuer has the `lsfAllowTrustLineClawback` flag set, that no offer exists between the parties, that the amount is non-zero, etc. — resides in the transaction handler in `src/xrpl/app/tx/impl/Clawback.cpp`. This header concerns itself only with the structural contract of the transaction, not its business rules.

The file sits at the boundary between raw protocol serialization (`STTx`, `STAmount`, `STObject`) and application-level code that needs to construct or inspect transactions without reaching into the serialization layer directly.