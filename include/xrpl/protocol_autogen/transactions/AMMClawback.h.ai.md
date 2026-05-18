# `AMMClawback.h` — Auto-Generated AMMClawback Transaction Wrapper

## Role and Context

This file is part of the `protocol_autogen` subsystem — a code-generated layer that provides type-safe C++ wrappers over the raw `STTx` serialized transaction format used throughout the XRPL node (`rippled`). Every transaction type in the XRPL protocol gets its own header in the `include/xrpl/protocol_autogen/transactions/` directory, and `AMMClawback.h` represents transaction type `ttAMM_CLAWBACK` (numeric type 31). The header comment warns explicitly: **do not edit** — modifications will be overwritten by the generator.

The `AMMClawback` transaction was introduced under the `featureAMMClawback` amendment. It allows a token issuer — one who has configured clawback authority — to recover tokens held by an account that has a position in an AMM (Automated Market Maker) pool. The transaction carries privileges beyond a normal transaction: `mayDeleteAcct | overrideFreeze | mayAuthorizeMPT`. This is significant because it means the engine grants elevated permissions during processing — the issuer can act across freeze rules and MPT authorization boundaries when reclaiming assets from an AMM.

## Two-Class Design: Wrapper and Builder

The file defines two classes that cleanly separate read and write concerns:

**`AMMClawback`** is an immutable, read-only wrapper around a `std::shared_ptr<STTx const>`. It inherits from `TransactionBase`, which stores the `tx_` pointer and provides getters for universal fields like `sfAccount`, `sfSequence`, `sfFee`, and optional fields like `sfFlags`, `sfMemos`, `sfDelegate`. `AMMClawback` adds the four `AMMClawback`-specific accessors: `getHolder()`, `getAsset()`, `getAsset2()`, and `getAmount()`.

The constructor validates the `STTx` type at runtime:
```cpp
if (tx_->getTxnType() != txType)
    throw std::runtime_error("Invalid transaction type for AMMClawback");
```
This guard prevents accidentally wrapping an unrelated transaction in a strongly-typed `AMMClawback` shell, which would otherwise silently return wrong field data through the typed accessors.

**`AMMClawbackBuilder`** inherits from `TransactionBuilderBase<AMMClawbackBuilder>` using CRTP (Curiously Recurring Template Pattern). The base class is templated on the derived type so that every common setter (`setAccount`, `setFee`, `setSequence`, `setMemos`, etc.) returns `Derived&` — preserving the fluent chaining interface through the full type hierarchy without virtual dispatch. The builder accumulates field data into an `STObject object_{sfTransaction}` and does not call `applyTemplate()` eagerly; the `STTx` constructor handles schema enforcement when `build()` is finally called.

## Field Schema and Optional Handling

The three required fields (`sfHolder`, `sfAsset`, `sfAsset2`) are set in the constructor, enforcing the protocol requirement that these must always be present. The optional field `sfAmount` follows the `getX()` / `hasX()` pair pattern used across all auto-generated types: `getAmount()` returns `protocol_autogen::Optional<SF_AMOUNT::type::value_type>` (an alias for `std::optional<...>`), deferring the presence check to `hasAmount()` before reading via `tx_->at(sfAmount)`.

The `setAsset` and `setAsset2` builder methods handle the `sfAsset` and `sfAsset2` fields with an explicit `STIssue` wrap:
```cpp
object_[sfAsset] = STIssue(sfAsset, value);
```
This is a subtle but important distinction from a plain assignment. `STIssue` is the serialized representation of an issue (currency + issuer, or MPT ID), and constructing it with the field descriptor ensures the correct field code is embedded for binary serialization. Both asset fields are annotated as supporting MPT (Multi-Purpose Token) amounts, reflecting the AMM subsystem's dual support for IOU and MPT token types.

## Build and Sign Flow

The `build(PublicKey, SecretKey)` method in `AMMClawbackBuilder` performs signing in-place via `TransactionBuilderBase::sign()`: it sets `sfSigningPubKey`, serializes the object without signing fields (prepended with `HashPrefix::txSign`), computes a signature with `xrpl::sign()`, and stores it in `sfTxnSignature`. The `STObject` is then moved into a freshly constructed `STTx`, which becomes the immutable payload of the returned `AMMClawback` wrapper. This one-way transformation — builder constructs, `AMMClawback` consumes — makes the type boundary explicit and prevents post-signing mutation.

There is also a second constructor for `AMMClawbackBuilder` that accepts an existing `std::shared_ptr<STTx const>`, copying its fields into the builder's mutable `STObject`. This round-trip path exists to support modifying and re-signing a previously built transaction.

## Relationship to the Auto-Gen System

`AMMClawback.h` is structurally identical to every other file in the `transactions/` directory (e.g., `AMMDeposit.h`, `AMMWithdraw.h`, `Clawback.h`). The only variation is the set of transaction-specific fields and the `txType` constant. `TransactionBase` and `TransactionBuilderBase` are the shared infrastructure; the per-transaction files are thin generated shells that expose exactly the fields defined in that transaction's protocol schema. This design means the generated code is predictable and auditable by diff, and the base classes can be improved independently without touching the ~70 generated transaction headers.