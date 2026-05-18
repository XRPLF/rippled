# `XChainModifyBridge.h` — Auto-Generated XChain Bridge Modification Transaction

This file is part of the `protocol_autogen` layer — a code-generated set of strongly-typed transaction wrappers built on top of XRPL's raw `STTx` serialization objects. It defines `XChainModifyBridge` (transaction type `ttXCHAIN_MODIFY_BRIDGE`, code 47), which allows the owner of a cross-chain bridge to update its operational parameters after creation. The file is gated behind the `featureXChainBridge` amendment and lives in the `xrpl::transactions` namespace alongside the full family of XChain transaction types.

## Transaction Purpose

Once a cross-chain bridge is live (created via `XChainCreateBridge`), its two tunable parameters — `sfSignatureReward` and `sfMinAccountCreateAmount` — may need adjustment as network conditions change. `XChainModifyBridge` is the only mechanism for doing so. It identifies the bridge to modify via the required `sfXChainBridge` composite field, then accepts either or both optional parameters. Because only the fields that need updating need to be supplied, the transaction can act as a partial update; callers don't need to repeat unchanged values.

## Wrapper/Builder Split

The file follows a strict two-class pattern shared across every transaction in `protocol_autogen/transactions/`:

**`XChainModifyBridge`** is the read-only, immutable wrapper. It takes ownership of a `shared_ptr<STTx const>` through `TransactionBase` and exposes typed accessors. The constructor performs a runtime type check, throwing `std::runtime_error` if the wrapped `STTx` is not actually `ttXCHAIN_MODIFY_BRIDGE`. This guard is necessary because `STTx` itself is a generic container — the wrapper's type safety would be meaningless without a validated construction point.

**`XChainModifyBridgeBuilder`** is the mutable construction surface, using CRTP by inheriting from `TransactionBuilderBase<XChainModifyBridgeBuilder>`. The template base provides all common field setters (`setFee`, `setSequence`, `setFlags`, `setDelegate`, etc.) and the protected `sign()` method. Because `setters` in the base return `Derived&` via `static_cast`, each call correctly returns `XChainModifyBridgeBuilder&` rather than a slice of the base — enabling unbroken fluent chains across both base and derived setters.

## Field Schema and Optionality

`sfXChainBridge` is the only required field in this transaction beyond the universal ones (account, sequence, fee). Its type is `SF_XCHAIN_BRIDGE::type::value_type`, an alias for `STXChainBridge` — a composite structure encoding the locking and issuing chain accounts and currencies. The builder takes it by `std::decay_t<...> const&`, stripping reference qualifiers so the assignment into `STObject` works cleanly regardless of the source value category.

`sfSignatureReward` and `sfMinAccountCreateAmount` are both `soeOPTIONAL` `SF_AMOUNT` fields, and the wrapper reflects this with paired accessor patterns: `getSignatureReward()` / `hasSignatureReward()` and `getMinAccountCreateAmount()` / `hasMinAccountCreateAmount()`. The getters return `protocol_autogen::Optional<SF_AMOUNT::type::value_type>` (i.e. `std::optional<STAmount>`) and short-circuit via the corresponding `has*()` call before touching `STTx::at()`, avoiding any risk of accessing a missing field. This is a deliberate contrast to how `XChainCreateBridge` treats `sfSignatureReward` — there it is `soeREQUIRED` and returned directly without wrapping in `optional`.

## Construction Paths

The builder offers two construction paths. The primary path takes an account plus the required `sfXChainBridge` field, then accepts optional sequence and fee arguments before delegating to `TransactionBuilderBase`. The secondary path accepts an existing `shared_ptr<STTx const>`, validates its type, and copies the `STObject` contents into `object_` — this enables round-tripping: load an already-existing transaction, adjust fields, re-sign, and produce a fresh `XChainModifyBridge`. The `STObject object_` member (declared in `TransactionBuilderBase` as `object_{sfTransaction}`) intentionally starts as a "free object" without an applied `SOTemplate`, sidestepping the restriction that `soeDEFAULT` fields cannot be explicitly set; the template is only applied later by the `STTx` constructor.

## Build and Sign

Calling `build(publicKey, secretKey)` finalises the transaction: it invokes `sign()` from the base (which serialises the object with `HashPrefix::txSign`, computes the signature, and writes `sfSigningPubKey` and `sfTxnSignature` into `object_`), then constructs a `shared_ptr<STTx>` from the mutated `STObject` and hands it to `XChainModifyBridge`'s constructor. After `build()` returns, the builder's internal `object_` has been moved out and is in a valid-but-unspecified state — the wrapper is the sole owner of the signed, immutable transaction.

## Relationship to the XChain Family

All eight XChain transaction headers in this directory share the same structural template, including the auto-generated comment at line 1. `XChainModifyBridge` is the narrowest of the set: it touches only an existing bridge's mutable parameters, while `XChainCreateBridge` establishes the bridge, `XChainCreateClaimID` / `XChainCommit` / `XChainClaim` move assets across it, and the attestation types (`XChainAddClaimAttestation`, `XChainAddAccountCreateAttestation`) handle witness quorum mechanics. Because the bridge identity itself (`sfXChainBridge`) is immutable once created, any request to change locking or issuing accounts must go through a full bridge teardown and recreation rather than through this transaction.