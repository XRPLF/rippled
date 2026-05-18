# `OfferCancel.h` — Auto-generated OfferCancel Transaction Wrapper

## Role and Context

This file is part of the `protocol_autogen` layer — a code-generated set of type-safe C++ wrappers over the XRPL's raw `STTx` serialized transaction format. Every transaction type in the ledger gets its own header here, following a uniform two-class pattern: an immutable reader and a fluent builder. `OfferCancel.h` encodes transaction type `ttOFFER_CANCEL` (8), which removes a previously placed order from the XRP Ledger's decentralized exchange (DEX).

The file carries the `// This file is auto-generated. Do not edit.` directive at line 1, placing it firmly in the machine-managed portion of the codebase. The entire `protocol_autogen/transactions/` directory contains ~70 such headers, one per ledger transaction type, all sharing the same structural skeleton while varying only in their transaction-specific fields.

## `OfferCancel` — The Immutable Wrapper

`OfferCancel` extends `TransactionBase`, a thin immutable shell that holds a `std::shared_ptr<STTx const>` and exposes type-safe accessors for the fields common to every transaction (account, sequence, fee, flags, memos, signers, etc.). The subclass adds exactly one transaction-specific accessor:

```cpp
[[nodiscard]]
SF_UINT32::type::value_type
getOfferSequence() const
{
    return this->tx_->at(sfOfferSequence);
}
```

`sfOfferSequence` is declared `soeREQUIRED`, meaning the ledger engine guarantees it is always present in a well-formed `OfferCancel`. The accessor therefore returns by value without an `std::optional` guard — a deliberate design choice that matches the field's optionality metadata and avoids forcing callers to unwrap an optional that can never be empty.

The constructor takes a `std::shared_ptr<STTx const>` and immediately validates the embedded transaction type:

```cpp
if (tx_->getTxnType() != txType)
    throw std::runtime_error("Invalid transaction type for OfferCancel");
```

This guard is the boundary between the untyped world of wire-format deserialization and the strongly typed autogen layer. Once construction succeeds the object can only represent a valid `ttOFFER_CANCEL`, making subsequent dispatch by type unnecessary and preventing misuse by callers who might accidentally wrap an `STTx` of the wrong kind.

The static `txType` constexpr member is available for compile-time dispatch or template specializations that need to map a C++ type back to its ledger integer constant.

## `OfferCancelBuilder` — The Fluent Builder

`OfferCancelBuilder` extends `TransactionBuilderBase<OfferCancelBuilder>`, which uses the Curiously Recurring Template Pattern (CRTP) to return `Derived&` from every setter, enabling method chaining without virtual dispatch or casting overhead. The base holds an `STObject object_{sfTransaction}` that accumulates field assignments before being moved into a final `STTx`.

The builder's primary constructor enforces `sfOfferSequence` as a required argument at the call site:

```cpp
OfferCancelBuilder(
    SF_ACCOUNT::type::value_type account,
    std::decay_t<typename SF_UINT32::type::value_type> const& offerSequence,
    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt)
```

`account` and `offerSequence` are positional and required; `sequence` and `fee` are optional because in some testing or pseudo-transaction contexts they may be filled in later. The design mirrors the ledger's own field optionality rules: required fields are constructor arguments, optional fields are fluent setters inherited from `TransactionBuilderBase`.

A second constructor takes an existing `std::shared_ptr<STTx const>` and copies its fields into the mutable `object_`, allowing an already-serialized transaction to be re-inflated into a builder for modification before re-signing. It performs the same type guard as the read-side wrapper.

`setOfferSequence()` returns `OfferCancelBuilder&`, completing the fluent chain and making it possible to override the value set at construction if needed.

The `build()` method finalizes the transaction:

```cpp
OfferCancel build(PublicKey const& publicKey, SecretKey const& secretKey)
{
    sign(publicKey, secretKey);
    return OfferCancel{std::make_shared<STTx>(std::move(object_))};
}
```

`sign()` is a protected method on `TransactionBuilderBase` that serializes the object with `HashPrefix::txSign`, signs it with the provided key pair, and stamps `sfSigningPubKey` and `sfTxnSignature` into the mutable `object_` before ownership is transferred. After `build()` the builder's `object_` is in a moved-from state and should not be reused.

## Design Tradeoffs

The immutability split between `OfferCancel` (read-only) and `OfferCancelBuilder` (write/build) is intentional. Once a transaction has been signed, its bytes must not change — any mutation would invalidate the cryptographic signature. By making `OfferCancel` wrap a `shared_ptr<STTx const>`, the type system enforces this invariant at compile time rather than relying on runtime checks.

The autogen approach trades flexibility for safety and consistency. `OfferCancel` has only one transaction-specific field — `sfOfferSequence` — which is the sequence number of the `OfferCreate` transaction that originally placed the order. There are no optional DEX-specific fields: no amount, no expiration, no flags unique to this type. The cancel transaction is intentionally minimal, relying entirely on the base-class machinery. In contrast, `OfferCreate` adds five or more fields covering `sfTakerPays`, `sfTakerGets`, `sfExpiration`, and `sfOfferSequence` (as an optional reference to a prior offer to replace). The structural symmetry across the autogen layer means that tooling or introspection code can rely on the same base interface regardless of which transaction type it encounters.

The `Delegation::delegable` annotation in the class comment indicates that `OfferCancel` supports the `sfDelegate` field, meaning a delegate account can submit this transaction on behalf of the originating account — a capability surfaced through `TransactionBase::getDelegate()` and `TransactionBuilderBase::setDelegate()` without any additional code in this file.