# `PaymentChannelCreate.h` — Auto-generated Transaction Wrapper and Builder

## Role and Context

This file is part of the `protocol_autogen` layer, a code-generated abstraction over XRPL's raw `STTx` serialized-transaction type. It defines two classes — `PaymentChannelCreate` and `PaymentChannelCreateBuilder` — representing the `ttPAYCHAN_CREATE` (type 13) transaction, which opens a unidirectional XRP payment channel on the ledger.

Payment channels exist to enable high-throughput, low-latency off-chain micropayments: the sender locks XRP into a channel object on the ledger, then issues cryptographically signed off-chain "claims" to a receiver. Only a final settlement requires an on-ledger transaction. `PaymentChannelCreate` is the first step in this lifecycle, followed by `PaymentChannelFund` (adding more XRP) and `PaymentChannelClaim` (redeeming a claim or closing the channel).

The file carries the `// This file is auto-generated. Do not edit.` guard and follows the identical structural pattern shared by every other transaction type in the `transactions/` directory. The generation ensures a uniform, consistent API surface across all ~70 transaction types.

## The Wrapper/Builder Split

`PaymentChannelCreate` is an immutable read accessor. It wraps a `std::shared_ptr<STTx const>` — the `const` qualifier on the template argument makes mutation impossible at the type level — and inherits common field getters (`getAccount()`, `getFee()`, `getSequence()`, etc.) from `TransactionBase`. Its only job is safe, named access to the fields of an existing, already-constructed transaction.

`PaymentChannelCreateBuilder` is the mutable counterpart, holding a live `STObject object_` and inheriting fluent setters for all common fields from `TransactionBuilderBase<PaymentChannelCreateBuilder>`. The CRTP pattern in the base class (`template <typename Derived>`) allows each `setXxx()` in the base to return `Derived&`, preserving the concrete type through the chain so callers never need a cast.

The lifecycle ends at `build(PublicKey, SecretKey)`, which calls the protected `sign()` helper from `TransactionBuilderBase`, serializes the `STObject` into a new `STTx`, and wraps it in the immutable `PaymentChannelCreate` type. After `build()` the builder's internal `object_` has been moved into the `STTx`, making the builder effectively spent.

## PaymentChannelCreate-Specific Fields

Four fields are `soeREQUIRED`, exposed as direct-value getters:

- **`sfDestination`** (`SF_ACCOUNT`): The account that can receive claims from this channel. Returned as `AccountID`.
- **`sfAmount`** (`SF_AMOUNT`): The amount of XRP (in drops) to lock into the channel at creation time.
- **`sfSettleDelay`** (`SF_UINT32`): The dispute window in seconds. When the destination requests channel closure, this is the grace period during which the source may still submit any outstanding claims. Choosing too short a value exposes the sender to race conditions; too long ties up XRP unnecessarily.
- **`sfPublicKey`** (`SF_VL`): The public key used to verify off-chain claim signatures. Critically, this is *not* the same key that signs the `PaymentChannelCreate` transaction itself — it is an application-level key dedicated to claim authentication and returned as a `Blob` (`SF_VL::type::value_type`).

Two fields are `soeOPTIONAL`, each paired with a `hasXxx()` predicate and a getter returning `protocol_autogen::Optional<T>`:

- **`sfCancelAfter`** (`SF_UINT32`): A Ripple-epoch timestamp after which the channel expires automatically, regardless of whether the destination has claimed anything. Useful for time-bounded payment guarantees.
- **`sfDestinationTag`** (`SF_UINT32`): A 32-bit routing tag on the destination side, analogous to `sfSourceTag` on the sender side.

## Constructor Type Enforcement

Both classes guard against misuse in their constructors. `PaymentChannelCreate(std::shared_ptr<STTx const>)` calls `tx_->getTxnType() != txType` and throws `std::runtime_error` immediately if the wrapped transaction is not actually a `ttPAYCHAN_CREATE`. The same check appears in `PaymentChannelCreateBuilder(std::shared_ptr<STTx const>)`, which handles the case of reconstructing a mutable builder from an existing transaction (useful for mutation or re-signing). This fail-fast pattern means type mismatches are surfaced at the point of wrapping, not silently deferred to field-access time.

## The `Optional<T>` Alias

Optional field getters return `protocol_autogen::Optional<T>`, defined in `Utils.h` as:

```cpp
template <typename ValueType>
using Optional = std::conditional_t<
    std::is_reference_v<ValueType>,
    std::optional<std::reference_wrapper<std::remove_reference_t<ValueType>>>,
    std::optional<ValueType>>;
```

This handles a subtlety in the XRPL field type system: some `SF_*` value types are reference types. If `ValueType` is a reference, wrapping it directly in `std::optional` would be ill-formed, so the alias redirects to `reference_wrapper`. For the fields present in this file (`SF_UINT32`), the value types are plain integers, so `Optional<uint32_t>` reduces straightforwardly to `std::optional<uint32_t>`.

## Builder Constructor and `std::decay_t`

The primary builder constructor takes all four required fields alongside optional `sequence` and `fee` parameters. The field-value parameters are typed as `std::decay_t<typename SF_XXX::type::value_type> const&` — stripping any reference or cv-qualifiers from the field's native value type before taking a const-reference. This ensures the setter signatures are stable and copyable regardless of how the underlying serialized-type system exposes its value type, and avoids inadvertently forming references-to-references.

The base class `TransactionBuilderBase` deliberately avoids calling `object_.set(soTemplate)` on the internal `STObject`. This is a non-obvious but important design decision: setting the SOTemplate would create `STBase` placeholder entries for `soeDEFAULT` fields, which then causes `applyTemplate()` — called later by the `STTx` constructor — to throw a "may not be explicitly set to default" error. By keeping the object "free" (no template applied), the builder allows the `STTx` constructor to handle template enforcement cleanly during finalization.