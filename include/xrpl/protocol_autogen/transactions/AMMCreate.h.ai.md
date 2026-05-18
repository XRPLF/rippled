# `AMMCreate.h` — Auto-Generated AMM Pool Creation Transaction Wrapper

## Role in the System

This file is part of the `protocol_autogen` layer — a code-generated façade over the XRPL core protocol types. Its job is to provide a strongly-typed, ergonomic C++ interface for the `AMMCreate` transaction (`ttAMM_CREATE`, type 35), which creates a new Automated Market Maker (AMM) liquidity pool on the XRP Ledger. Without this layer, callers would interact directly with `STTx` using untyped field tags and raw serialization objects. The auto-generated wrapper ensures field access is statically checked, mandatory fields are present at construction time, and the build-then-seal lifecycle is enforced through the type system.

The file lives in `xrpl::transactions` alongside dozens of sibling wrappers for every other transaction type on the ledger. All follow the same two-class pattern established here.

## The Two-Class Design

`AMMCreate` is an immutable read-only wrapper. It holds a `std::shared_ptr<STTx const>` inherited from `TransactionBase` and provides three typed getters — `getAmount()`, `getAmount2()`, and `getTradingFee()` — for the fields that are specific to this transaction type. The base class `TransactionBase` provides all universal getters: `getAccount()`, `getFee()`, `getSequence()`, `getMemos()`, and so on. Because `STTx` itself is immutable once constructed, there is no need for locking or defensive copying in the read path.

`AMMCreateBuilder` is the mutable counterpart, inheriting from the CRTP base `TransactionBuilderBase<AMMCreateBuilder>`. It holds a live `STObject` and exposes fluent setters that each return `AMMCreateBuilder&`, enabling method chaining. When the caller has set all desired fields they call `build(publicKey, secretKey)`, which signs the transaction — serializing it with `HashPrefix::txSign` and computing the `TxnSignature` — then constructs an `AMMCreate` from the resulting `STTx`. After `build()` returns, mutation is no longer possible; the builder's `STObject` has been moved into the immutable `STTx`.

The design consciously avoids a single mutable class. A class that is both a builder and a reader would allow callers to observe partially-constructed state, and would make it impossible to reason about field invariants. By splitting responsibilities, `AMMCreate` can be passed around freely knowing that its three required fields are always present.

## AMM-Specific Fields

`AMMCreate` requires exactly three transaction-specific fields:

- **`sfAmount`** and **`sfAmount2`** — the initial deposit amounts for the two assets that seed the liquidity pool. Both are typed as `SF_AMOUNT::type::value_type`, which in the current codebase covers both `STAmount` (XRP or IOU) and MPT (Multi-Purpose Token) amounts. The `@note` in the doc comments explicitly flags this, since MPT support is a newer addition under the `featureAMM` amendment, and callers need to know that token types beyond the classical XRP/IOU dichotomy are valid.

- **`sfTradingFee`** — a `uint16` fee rate expressed in basis points, charged on every trade against the pool. It is required at pool creation and cannot be changed without a separate `AMMVote` transaction.

The constructor for `AMMCreateBuilder` demands all three up front. This is the correct design: there is no valid `AMMCreate` without two assets and a trading fee, so deferring them to optional setters would just push a runtime error to a later, harder-to-diagnose point.

## Type Validation and Failure Modes

Both `AMMCreate` and `AMMCreateBuilder` validate the transaction type on construction and throw `std::runtime_error` if the wrapped `STTx` is not a `ttAMM_CREATE`. This guard matters for the second constructor of `AMMCreateBuilder`, which accepts an existing `STTx` to create an editable copy — a pattern used in testing and transaction mutation workflows. Without the guard, a caller could accidentally wrap an unrelated transaction type and silently corrupt fields.

`TransactionBase::validate()` provides a deeper schema check by running `validateSTObject` against the `TxFormats` template for this transaction type, followed by `passesLocalChecks`. This is not called automatically on construction; it is an explicit post-construction gate.

## Amendment and Privilege Metadata

The class doc records that `AMMCreate` requires the `featureAMM` amendment, carries the `Delegation::delegable` flag (meaning a delegated account can submit it on behalf of the primary), and holds the `createPseudoAcct | mayCreateMPT` privileges. These are not enforced in this header — they are enforced by the ledger's transaction processing logic — but documenting them inline on the wrapper class makes it immediately clear what ledger conditions must be met before this transaction type can be submitted.

## Relationship to the Auto-Generated Layer

This file is generated, not hand-authored. The comment on line 1 makes that explicit. The practical consequence is that it should never be edited directly; changes to `AMMCreate`'s field set belong in whatever template or schema drives code generation. The file's uniformity with every other sibling in `protocol_autogen/transactions/` — same two-class pattern, same constructor guards, same CRTP base — is a deliberate outcome of generation, making the entire transaction API consistent and predictable.