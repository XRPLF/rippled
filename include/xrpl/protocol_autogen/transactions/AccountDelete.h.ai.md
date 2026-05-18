# `AccountDelete.h` — Auto-Generated AccountDelete Transaction Wrapper

## Role and Context

This header is part of the `protocol_autogen` subsystem, a code-generated layer that provides a type-safe, ergonomic C++ API over the XRPL wire-protocol transaction types. It lives alongside roughly seventy other per-transaction headers in `include/xrpl/protocol_autogen/transactions/`, all following an identical structural pattern: an immutable reader class paired with a fluent builder class. The comment at line 1 — `// This file is auto-generated. Do not edit.` — signals that the source of truth is a code-generation template, not this file itself; hand-edits would be clobbered on regeneration.

`AccountDelete` is the XRPL mechanism for permanently closing an account and sending its remaining XRP balance to a specified destination. Because closing an account is irreversible and has strict eligibility requirements (the account must have a low enough sequence number relative to the current ledger), the transaction type carries additional metadata: it is tagged `Delegation::notDelegable` (cannot be submitted on behalf of another account via the delegate mechanism) and requires the `mustDeleteAcct` privilege. Its transaction type constant is `ttACCOUNT_DELETE` (21).

## Class Structure: Reader/Builder Split

The file defines two classes with deliberately asymmetric responsibilities.

### `AccountDelete` — Immutable Wrapper

`AccountDelete` extends `TransactionBase` and wraps a `std::shared_ptr<STTx const>` — shared ownership over an immutable, already-signed transaction object. `TransactionBase` itself (defined in `TransactionBase.h`) provides accessors for the universal header fields common to every XRPL transaction: `sfAccount`, `sfSequence`, `sfFee`, `sfSigningPubKey`, and optional fields like `sfFlags`, `sfMemos`, `sfSigners`, `sfLastLedgerSequence`, `sfDelegate`, and others. `AccountDelete` adds only the fields specific to its transaction type.

The constructor is the single enforcement point for type correctness:

```cpp
explicit AccountDelete(std::shared_ptr<STTx const> tx)
    : TransactionBase(std::move(tx))
{
    if (tx_->getTxnType() != txType)
        throw std::runtime_error("Invalid transaction type for AccountDelete");
}
```

Note the subtle bug-prevention here: `tx` is moved into the base class before the type check reads `tx_`. Since `move(tx)` leaves `tx` null, the guard uses `tx_` (the base class member), not the local parameter. This is correct because `TransactionBase` stores it immediately via `tx_(std::move(tx))`.

The three transaction-specific accessors expose AccountDelete's fields:

- **`getDestination()`** — returns the required `sfDestination` field as an `AccountID`. No nullopt path, since this field is `soeREQUIRED` and always present in a valid transaction.
- **`getDestinationTag()`** / **`hasDestinationTag()`** — an optional 32-bit tag that routes the payment within the destination account's system. The pattern of separating presence check from value retrieval (`has*` + `get*`) avoids exceptions from accessing absent fields and is consistent across the entire autogen layer.
- **`getCredentialIDs()`** / **`hasCredentialIDs()`** — an optional vector of 256-bit credential identifiers. The `sfCredentialIDs` field supports the XRPL Credentials amendment, allowing the sending account to prove eligibility for deletion via verifiable credentials rather than purely through ledger state checks.

Return types for optional fields use `protocol_autogen::Optional<T>`, a type alias defined in `Utils.h`:

```cpp
template <typename ValueType>
using Optional = std::conditional_t<
    std::is_reference_v<ValueType>,
    std::optional<std::reference_wrapper<std::remove_reference_t<ValueType>>>,
    std::optional<ValueType>>;
```

This alias transparently handles the case where `ValueType` is a reference (wrapping it in `std::reference_wrapper` to satisfy `std::optional`'s non-reference constraint) versus a value type (using `std::optional<ValueType>` directly). In practice, `SF_UINT32::type::value_type` and `SF_VECTOR256::type::value_type` are values, so these resolve to plain `std::optional`.

All getters are marked `[[nodiscard]]`, making it a compile-time warning to silently discard a return value — a minor defensive measure appropriate for an API layer used in transaction processing code.

### `AccountDeleteBuilder` — Fluent Mutable Builder

`AccountDeleteBuilder` extends `TransactionBuilderBase<AccountDeleteBuilder>`, a CRTP base class. The CRTP pattern is essential here: the base class setter methods return `Derived&` (not `TransactionBuilderBase&`), which means every call in a chain returns the concrete `AccountDeleteBuilder` type and can be continued with `AccountDeleteBuilder`-specific setters without casts. This is how fluent chaining works across the base and derived layers simultaneously.

The builder holds a mutable `STObject object_{sfTransaction}` (declared in `TransactionBuilderBase`). The design deliberately avoids calling `object_.set(soTemplate)` during construction, which is explained in a comment in `TransactionBuilderBase`:

> "This avoids creating STBase placeholders for soeDEFAULT fields, which would cause `applyTemplate()` to throw 'may not be explicitly set to default' when building the `STTx`."

In other words, the `STObject` is kept in a "free" state until promoted to `STTx` at `build()` time, when the `STTx` constructor calls `applyTemplate()` to validate and fill default fields correctly.

The primary constructor enforces that `sfDestination` is set immediately — it is a required field and would cause a malformed transaction if omitted. Optional fields `sfDestinationTag` and `sfCredentialIDs` are set through dedicated setters returning `AccountDeleteBuilder&` for chaining.

The builder can also be constructed from an existing `std::shared_ptr<STTx const>`, copying the existing transaction's data into the mutable `object_` (`object_ = *tx`). This supports use cases like modifying an existing AccountDelete transaction — for example, updating the fee — before re-signing.

**`build()`** is the terminal operation. It calls the `sign()` method inherited from `TransactionBuilderBase`, which serializes the transaction data with the `HashPrefix::txSign` prefix and signs it with the provided key pair, then constructs a new `STTx` from the (now signed) `STObject` and wraps it in an `AccountDelete` read-only wrapper. After `build()`, the `object_` has been moved away and the builder is effectively consumed.

## Relationship to the Broader `protocol_autogen` Layer

`AccountDelete.h` is one of roughly seventy identically-structured headers in the `transactions/` directory, covering every XRPL transaction type from `AMMBid` to `XChainModifyBridge`. The uniformity is deliberate: any code that processes or inspects XRPL transactions can rely on a consistent accessor contract. The base classes `TransactionBase` and `TransactionBuilderBase<T>` centralize common logic (common field getters, the signing procedure, memos/signers handling) so the generated files contain only the minimum per-type differentiation: which fields exist, whether they are required or optional, and their wire types.