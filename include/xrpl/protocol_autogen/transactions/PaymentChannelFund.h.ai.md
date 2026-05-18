# `PaymentChannelFund.h` — Auto-generated Payment Channel Funding Transaction

## Role in the System

This file is part of the `xrpl/protocol_autogen/transactions/` layer — a code-generated collection of per-transaction-type C++ headers that provide strongly-typed interfaces over the raw `STTx` serialized transaction format. It handles the `ttPAYCHAN_FUND` (type 14) transaction, which replenishes an existing XRPL payment channel with additional XRP and optionally extends its expiration deadline. The file lives alongside `PaymentChannelCreate.h` (type 13) and `PaymentChannelClaim.h`, together covering the full lifecycle of XRPL payment channels.

The "do not edit" header is significant: this file is generated from a transaction schema definition, ensuring that as the protocol evolves, the C++ bindings remain consistent without hand-maintenance drift.

## The Two-Class Pattern

Every transaction type in this layer is expressed as a pair of classes following the same structural contract:

**`PaymentChannelFund`** is the immutable read-side. It wraps a `std::shared_ptr<STTx const>` and delegates to `TransactionBase`, which itself stores that shared pointer. The `const` on the pointed-to object is the key invariant: once constructed, the underlying serialized transaction data cannot change. The constructor throws `std::runtime_error` immediately if the wrapped `STTx` is not of type `ttPAYCHAN_FUND`, so the type mismatch is caught at construction time rather than silently producing garbage field reads later.

**`PaymentChannelFundBuilder`** is the mutable write-side. It inherits from `TransactionBuilderBase<PaymentChannelFundBuilder>` using CRTP so that all common field setters (`setFee`, `setSequence`, `setLastLedgerSequence`, etc.) return `PaymentChannelFundBuilder&` rather than the base type — enabling fully typed method chaining without a cast. Internally the builder holds an `STObject` (not an `STTx`) until `build()` is called.

This separation is deliberate: separating read access from write access means callers who only inspect transactions never have access to mutation methods, and callers constructing transactions work with a dedicated type that cannot be confused with a validated, signed wrapper.

## Fields and Optionality

`PaymentChannelFund` exposes three transaction-specific fields beyond the common ones inherited from `TransactionBase`:

- **`sfChannel`** (`soeREQUIRED`) — a 256-bit identifier (`SF_UINT256::type::value_type`) that names the existing payment channel to fund. Passed by the builder constructor and enforced as required at the protocol level; `getChannel()` calls `tx_->at(sfChannel)` which will throw if somehow absent (guarded by the serialization layer upstream).

- **`sfAmount`** (`soeREQUIRED`) — the quantity of XRP drops to add to the channel's reserve (`SF_AMOUNT::type::value_type` resolves to `STAmount`). Required both in the builder constructor and in the protocol definition; `getAmount()` uses the same direct `at()` accessor.

- **`sfExpiration`** (`soeOPTIONAL`) — a `uint32_t` ripple epoch timestamp. When present, this sets a new upper bound on when the channel may be closed by the source account alone (the destination or transaction expiration can still close it earlier). The absence of this field is meaningful — it means no change to the current expiration — so the accessor returns `protocol_autogen::Optional<SF_UINT32::type::value_type>` rather than a raw value.

The `protocol_autogen::Optional<T>` alias in `Utils.h` is a small but important detail. It uses `std::conditional_t` to produce either `std::optional<std::reference_wrapper<std::remove_reference_t<T>>>` (when `T` is a reference type) or `std::optional<T>` (when it is not). This handles the case where `SF_*::type::value_type` might be a reference to an internal buffer — returning an `optional<reference_wrapper<…>>` avoids dangling references while still letting callers detect absence. For `sfExpiration`'s `uint32_t` this resolves to a plain `std::optional<uint32_t>`, but the alias keeps the pattern uniform across all auto-generated files.

The `hasExpiration()` / `getExpiration()` pairing is consistent with all optional fields in this layer: the `has*` method calls `isFieldPresent()`, and `get*` checks that same predicate before calling `at()`. This avoids an exception from `STObject::at()` when a field is absent.

## Builder Construction and Signing

`PaymentChannelFundBuilder` offers two construction paths:

1. **Fresh construction** takes `account`, `channel`, and `amount` as required arguments, with `sequence` and `fee` defaulting to `std::nullopt`. The constructor delegates to `TransactionBuilderBase` (which sets `sfTransactionType`, `sfAccount`, and optionally `sfSequence`/`sfFee`) and then calls `setChannel` and `setAmount` immediately. Sequence and fee can be deferred because `TransactionBuilderBase` conditionally sets them only when the optionals carry values — this accommodates workflows where fee calculation or sequence lookup happens after initial construction.

2. **Round-trip construction** from an existing `std::shared_ptr<STTx const>` copies the `STTx`'s field data into the builder's `STObject object_` via `object_ = *tx`. This enables a modify-and-resign workflow: deserialize a transaction, wrap it in the builder, call additional setters to mutate fields, then call `build()` to produce a freshly signed `PaymentChannelFund`. The type guard `tx->getTxnType() != ttPAYCHAN_FUND` applies here too.

`build(PublicKey, SecretKey)` calls `sign()` (inherited from `TransactionBuilderBase`), which serializes the current `STObject` with `HashPrefix::txSign` and computes the `sfTxnSignature`. It then constructs a `STTx` from the mutated `STObject` via `std::make_shared<STTx>(std::move(object_))` and wraps it in a `PaymentChannelFund`. The move is intentional — the builder's internal state is consumed and should not be used after `build()`.

The `std::decay_t<typename SF_UINT256::type::value_type>` pattern used in setter parameters strips away references and cv-qualifiers from the field's value type. This is necessary because `SF_*::type::value_type` may be defined as a `const T&` in some field descriptors; without `decay_t`, the setter parameter type would collapse to a reference-to-reference, which is ill-formed. Using `decay_t` ensures the setter always takes a `const T&` argument in the conventional sense.

## Relationship to Sibling Files

`PaymentChannelCreate.h` establishes a new channel with `sfDestination`, `sfSettleDelay`, and `sfPublicKey`. `PaymentChannelFund.h` references only `sfChannel` (the identifier of that already-created channel) to top it up. The structural symmetry between these files is exact by design — both follow the same generated pattern, both carry `Delegation::delegable` (the transaction can be executed by a delegate account via `sfDelegate`), and neither requires a protocol amendment (`uint256{}`).