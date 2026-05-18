# `AMMVote.h` — Auto-Generated Type-Safe Wrapper for AMM Trading Fee Vote Transactions

## Role and Context

This header is part of the `xrpl/protocol_autogen/transactions/` layer — a code-generated family of transaction wrappers that provide type-safe, ergonomic access to XRPL's serialized transaction types. The file should never be hand-edited; changes belong at the generator level.

`AMMVote` wraps the `ttAMM_VOTE` (type 38) transaction, which is the on-chain mechanism by which liquidity providers (LP token holders) participate in governance of an AMM pool's trading fee. The economics are documented in the transactor at `include/xrpl/tx/transactors/dex/AMMVote.h`: a weighted average of all active votes, weighted by each voter's LP token balance, determines the live `TradingFee`. Up to eight vote slots are tracked in the `ltAMM` ledger object's `VoteSlots` array. This file encodes that protocol's transaction structure into C++ types, gated by the `featureAMM` amendment.

## Two-Class Design: Wrapper and Builder

The file defines exactly two classes following a pattern replicated across every autogen transaction type:

**`AMMVote`** extends `TransactionBase` and is an *immutable* read-only wrapper around a `std::shared_ptr<STTx const>`. Its only job is to provide named, strongly-typed field accessors over the opaque `STTx` map. There are no mutation methods — once constructed, the underlying transaction cannot be altered. The constructor performs a type guard by checking `tx_->getTxnType() != txType` via the protected `tx_` member inherited from `TransactionBase`, throwing `std::runtime_error` on mismatch. Because the base class initializer list runs before the constructor body, this check safely accesses the already-stored pointer.

**`AMMVoteBuilder`** extends `TransactionBuilderBase<AMMVoteBuilder>` using CRTP (Curiously Recurring Template Pattern). The base uses `static_cast<Derived&>(*this)` on every setter to return the concrete subclass type, enabling fluent method chaining without virtual dispatch and without losing the concrete type in the chain. The builder holds a mutable `STObject object_` (initialized in the base as `sfTransaction`) and populates it field by field.

## Required Fields

The three transaction-specific required fields (`soeREQUIRED`) are:

- **`sfAsset`** (`SF_ISSUE`) — identifies the first asset of the AMM pool being voted on.
- **`sfAsset2`** (`SF_ISSUE`) — identifies the second asset. Together, the two asset fields uniquely identify the AMM instance on-ledger, since there is exactly one AMM per ordered asset pair.
- **`sfTradingFee`** (`SF_UINT16`) — the voter's proposed fee, expressed in basis points.

Both `sfAsset` fields carry a note that they support MPT (Multi-Purpose Token) amounts, reflecting the extension of AMM to handle non-IOU asset types.

Setter methods on `AMMVoteBuilder` use `std::decay_t<typename SF_ISSUE::type::value_type>` as the parameter type. The `std::decay_t` strips const and reference qualifiers from whatever `SF_ISSUE::type::value_type` resolves to, ensuring the parameter type is a plain value type suitable for `const&` argument passing and copy construction. This is a recurring pattern across all autogen setters to handle field types that might be reference-like aliases.

Asset fields are assigned as `STIssue(sfField, value)` explicitly rather than by direct operator assignment, because `STObject::operator[]` for an `SF_ISSUE` field expects the inner `STIssue` wrapper type, not the raw `value_type` directly.

## Builder Construction Paths

`AMMVoteBuilder` offers two constructors. The primary constructor accepts all required fields — `account`, `asset`, `asset2`, `tradingFee` — plus optional `sequence` and `fee`. This is the normal construction path. The secondary constructor accepts a `std::shared_ptr<STTx const>` directly, copying the existing `STTx` into `object_` via the `STObject` copy constructor (`object_ = *tx`). This path is useful for round-tripping or modifying a previously-deserialized transaction while retaining the builder API.

## Finalizing with `build()`

`AMMVoteBuilder::build(publicKey, secretKey)` calls the base class `sign()` method, which sets `sfSigningPubKey`, computes `HashPrefix::txSign + serialized fields (excluding signing fields)`, signs the payload with the provided keys, and stores the signature in `sfTxnSignature`. It then constructs a new `STTx` by moving `object_` and wraps it in an `AMMVote`. After `build()` returns, the builder's internal `object_` is in a moved-from state and should not be reused.

## Relationship to `TransactionBase`

`TransactionBase` supplies all common transaction field accessors: `getAccount()`, `getSequence()`, `getFee()`, `getFlags()`, `getLastLedgerSequence()`, `getMemos()`, `getSigners()`, `getDelegate()`, and others. The delegation-aware `sfDelegate` field accessor is present because `AMMVote` is marked `Delegation::delegable`, meaning another account can submit this vote transaction on behalf of the LP token holder.