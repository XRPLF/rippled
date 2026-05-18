# `XChainCreateBridge.h` — Auto-generated XChain Bridge Creation Transaction

## Role in the System

This file, located in `include/xrpl/protocol_autogen/transactions/`, is an auto-generated header that provides the C++ interface for the `XChainCreateBridge` transaction — transaction type `ttXCHAIN_CREATE_BRIDGE` (type code 48). It belongs to the `featureXChainBridge` amendment family and operates within the `xrpl::transactions` namespace.

The `XChainCreateBridge` transaction initiates the existence of a cross-chain bridge on the XRP Ledger, allowing assets to be moved between two separate ledgers (typically XRPL mainnet and a sidechain, or two independent XRPL networks). Once submitted and validated, this transaction establishes the on-ledger `Bridge` object that subsequent cross-chain transactions — commits, claims, and attestations — reference by its `sfXChainBridge` identity.

The file pairs a read-only wrapper class with a fluent builder class, following the same autogeneration pattern applied uniformly across all ~70 transaction types in this directory.

## Design: Immutable Wrapper + Fluent Builder

The file follows the same split that every transaction type in `protocol_autogen/transactions/` uses:

**`XChainCreateBridge`** is an immutable, read-only view over a `std::shared_ptr<STTx const>`. It inherits from `TransactionBase`, which holds the shared pointer as `tx_` and provides all common field accessors (`getAccount()`, `getSequence()`, `getFee()`, `getDelegate()`, etc.). `XChainCreateBridge` adds only the three fields specific to its transaction type. Immutability is enforced both at the pointer level (`const` `STTx`) and by the class offering no setter methods — the only way to mutate is through the builder.

**`XChainCreateBridgeBuilder`** is a mutable accumulator using CRTP (curiously recurring template pattern) via `TransactionBuilderBase<XChainCreateBridgeBuilder>`. The base class stores an `STObject object_{sfTransaction}` and provides setters for all common fields. The derived builder adds transaction-specific setters and, importantly, the terminal `build()` method, which calls `sign()` on the base, then wraps the accumulated `STObject` into a freshly allocated `STTx` and returns it inside an `XChainCreateBridge` wrapper. This one-way flow from builder to signed wrapper enforces that callers cannot hold a mutable reference after signing.

A deliberate design choice in `TransactionBuilderBase` is to *not* call `object_.set(soTemplate)` — the comment in that base explains why: calling `applyTemplate()` before `STTx` construction would create default-value placeholders for `soeDEFAULT` fields, which then cause `applyTemplate()` inside the `STTx` constructor to throw "may not be explicitly set to default." The builder therefore operates on a free `STObject` and lets `STTx`'s own constructor handle template application.

## Fields

`XChainCreateBridge` carries three fields beyond the universal base fields:

**`sfXChainBridge`** (`soeREQUIRED`) — a composite field of type `STXChainBridge` that encodes the four identifiers defining the bridge: the locking-chain issuing account, the issuing-chain issuing account, the locking-chain door account, and the issuing-chain door account. This serves as the bridge's stable identifier referenced by all subsequent cross-chain operations.

**`sfSignatureReward`** (`soeREQUIRED`) — an `STAmount` specifying the XRP paid to witnesses for each valid attestation they submit. Making this required at bridge creation is a deliberate protocol decision: a bridge without a reward has no economic mechanism to attract witness participation, so permitting its absence would create non-functional bridges. Compare `XChainModifyBridge`, where `sfSignatureReward` is *optional* because modifications may target only the `sfMinAccountCreateAmount` without changing the reward.

**`sfMinAccountCreateAmount`** (`soeOPTIONAL`) — an `STAmount` specifying the minimum XRP that must be attached to an `XChainAccountCreateCommit` transaction. This field exists only for XRP-native bridges where the destination chain requires a funded new account. IOU/token bridges do not use this field because they require the destination account to exist already; that makes optionality load-bearing rather than cosmetic.

## Optional Field Handling

The return type of `getMinAccountCreateAmount()` is `protocol_autogen::Optional<SF_AMOUNT::type::value_type>`, a type alias defined in `Utils.h`. That alias is:

```cpp
template <typename ValueType>
using Optional = std::conditional_t<
    std::is_reference_v<ValueType>,
    std::optional<std::reference_wrapper<std::remove_reference_t<ValueType>>>,
    std::optional<ValueType>>;
```

This distinguishes reference types (like `STArray const&` returned from `getMemos()`) from value types (like `STAmount`). For `sfMinAccountCreateAmount`, the result is simply `std::optional<STAmount>`. The getter guards access with `hasMinAccountCreateAmount()` before calling `tx_->at(...)`, preventing an exception from `STObject::at()` when the field is absent.

## Type Safety and Fail-Fast Validation

Both the wrapper constructor and the STTx-initializing builder constructor perform an explicit type check against `ttXCHAIN_CREATE_BRIDGE`, throwing `std::runtime_error` on mismatch. This fail-fast guard prevents silent misuse when, for example, a caller holds a generic `std::shared_ptr<STTx const>` retrieved from the ledger and passes it to the wrong wrapper type.

The static `constexpr txType` member allows compile-time dispatch in template code that dispatches on transaction type without a virtual call, consistent with how the autogeneration layer avoids runtime polymorphism beyond what `STTx` itself already provides.

## Relationship to Sibling XChain Files

`XChainCreateBridge.h` sits alongside six other XChain transaction headers: `XChainModifyBridge`, `XChainCommit`, `XChainAccountCreateCommit`, `XChainCreateClaimID`, `XChainClaim`, `XChainAddClaimAttestation`, and `XChainAddAccountCreateAttestation`. The bridge created here is referenced by all of them. `XChainModifyBridge` is the closest sibling — it shares the same `sfXChainBridge` and `sfMinAccountCreateAmount` fields, but promotes `sfSignatureReward` from required to optional, reflecting that post-creation modifications are incremental.