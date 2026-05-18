# `include/xrpl/protocol_autogen/transactions/AMMBid.h`

This file is part of the `protocol_autogen` subsystem — a layer of auto-generated, type-safe C++ wrappers over the raw `STTx` serialized transaction format. It defines two classes for the `AMMBid` transaction type: `AMMBid`, an immutable read accessor, and `AMMBidBuilder`, a fluent construction interface. The header carries an explicit `// This file is auto-generated. Do not edit.` guard and should never be modified by hand.

## Purpose in the XRPL AMM Model

`AMMBid` corresponds to transaction type `ttAMM_BID` (numeric code 39), introduced under the `featureAMM` amendment. On the XRP Ledger, each AMM pool has a single "auction slot" — a position that, when won via a bid of LP tokens, grants the slot holder a reduced trading fee for a fixed time window. The `AMMBid` transaction is how an LP token holder competes for that slot, optionally bounding their bid with a minimum (`sfBidMin`) and maximum (`sfBidMax`) amount, and optionally delegating the fee discount to a list of additional accounts (`sfAuthAccounts`).

The transaction metadata in the class comment records that it is *delegable* (`Delegation::delegable`), meaning a delegate account (`sfDelegate`, inherited from `TransactionBase`) may submit it on behalf of the originating account.

## Class Design: Immutable Wrapper + Builder Pair

The file follows a deliberate split present throughout the `protocol_autogen/transactions/` directory: every transaction type is represented by a read-only wrapper and a separate builder, never a single mutable class. This enforces the immutability contract of `STTx` — once a transaction is signed and serialized, it must not change.

**`AMMBid`** inherits from `TransactionBase`, which holds a `std::shared_ptr<STTx const>` (the `const` is load-bearing). The base exposes accessors for universal fields like `getAccount()`, `getSequence()`, `getFee()`, `getFlags()`, and others shared by all transaction types. `AMMBid` adds only the fields specific to this transaction type.

The constructor validates the wrapped transaction's type immediately and throws `std::runtime_error` if it does not match `ttAMM_BID`. This is an early-failure defense — it prevents a misidentified `STTx` from silently producing wrong field reads later.

## Field Model: Required vs. Optional

The two asset fields — `getAsset()` and `getAsset2()` — are marked `soeREQUIRED` and return `SF_ISSUE::type::value_type` directly (no `optional`). Together they uniquely identify the AMM pool being bid on: `sfAsset` and `sfAsset2` name the two currencies in the pool. Both fields are declared as `SF_ISSUE` types, and the comments note that they support MPT (Multi-Purpose Token) amounts, anticipating the ledger's expanded token model.

The bid range fields `getBidMin()` and `getBidMax()` are `soeOPTIONAL`. They use the `protocol_autogen::Optional<T>` alias defined in `Utils.h`:

```cpp
template <typename ValueType>
using Optional = std::conditional_t<
    std::is_reference_v<ValueType>,
    std::optional<std::reference_wrapper<std::remove_reference_t<ValueType>>>,
    std::optional<ValueType>>;
```

This alias exists because some XRPL field types return values by reference (e.g., complex `STObject` wrappers), while others return plain values. Wrapping both uniformly in `std::optional` would fail for reference types, since `std::optional<T&>` is ill-formed in C++17. The `protocol_autogen::Optional` selects the right form at compile time. For `SF_AMOUNT` values (which are returned by value), `Optional<SF_AMOUNT::type::value_type>` simply reduces to `std::optional<STAmount>`.

The `sfAuthAccounts` field is the exception: it holds an `STArray` — the ledger's heterogeneous inner-object array type — and is therefore returned as `std::optional<std::reference_wrapper<STArray const>>` directly, bypassing `protocol_autogen::Optional`. This is consistent with how `getMemos()` and `getSigners()` work in `TransactionBase`. Returning a `std::reference_wrapper` instead of copying avoids potentially expensive deep copies of the array while still allowing the caller to detect absence cleanly.

Every optional getter has a corresponding `has*()` predicate (`hasBidMin()`, `hasBidMax()`, `hasAuthAccounts()`). The pattern is consistent: call `isFieldPresent()` on the underlying `STTx` before forwarding to `tx_->at()` or `getFieldArray()`.

## `AMMBidBuilder`: CRTP Fluent Construction

`AMMBidBuilder` inherits from `TransactionBuilderBase<AMMBidBuilder>` using the Curiously Recurring Template Pattern. The base class holds an `STObject object_{sfTransaction}` and provides setters for universal fields (`setAccount`, `setFee`, `setSequence`, `setFlags`, etc.), each returning `Derived&` — here, `AMMBidBuilder&` — enabling full method chaining without any virtual dispatch.

The primary constructor takes `account`, `asset`, and `asset2` as required parameters, with `sequence` and `fee` as optional. It immediately calls `setAsset()` and `setAsset2()`, ensuring the two invariant fields are always present before the builder is used. A secondary constructor accepts an existing `std::shared_ptr<STTx const>` for editing a pre-built transaction, which is useful in test and tooling contexts.

Field setters for `sfAsset` and `sfAsset2` explicitly construct `STIssue(sfField, value)` before assignment, handling the type conversion from the `SF_ISSUE::type::value_type` domain into the `STObject` field store. The `setBidMin` and `setBidMax` setters assign `SF_AMOUNT` values directly. `setAuthAccounts` delegates to `object_.setFieldArray()` since `STArray` fields require a dedicated path through the `STObject` API.

The `build()` method calls the protected `sign()` helper from `TransactionBuilderBase`, which serializes the object with `HashPrefix::txSign` prefix, computes the ECDSA/Ed25519 signature, and embeds the signing public key. It then moves the `STObject` into a new `STTx` and wraps the result in an `AMMBid` instance. After `build()` is called, the builder's internal `object_` has been moved-from and should not be reused.

## Position in the Autogen Layer

`AMMBid.h` is one of roughly 70 transaction-specific headers in the `protocol_autogen/transactions/` directory. All follow the same structural template: the same include list, the same constructor guard pattern, the same `TransactionBase` / `TransactionBuilderBase<Derived>` inheritance chain. The variation between files is entirely in which fields are `soeREQUIRED` versus `soeOPTIONAL` and what types they carry. This regularity is what makes automated generation feasible and reliable, and it is also why the `// Do not edit.` warning matters — any manual patch would be overwritten by the next code-generation run.