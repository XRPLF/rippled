# `XChainAccountCreateCommit.h` — Cross-Chain Account Creation Transaction

## Purpose and Protocol Context

This file is part of the `protocol_autogen` layer — a collection of auto-generated, type-safe C++ wrappers over XRPL's raw `STTx` transaction objects. It defines the `XChainAccountCreateCommit` transaction (`ttXCHAIN_ACCOUNT_CREATE_COMMIT`, type 44), which is gated behind the `featureXChainBridge` amendment.

Within the XChain bridge protocol, this transaction is the first step in creating a new account on the destination chain. A user on the locking chain submits this transaction to commit funds; the bridge witnesses observe the event, attest to it (via `XChainAddAccountCreateAttestation`), and once a quorum is reached, the new account is funded on the issuing chain. The four required domain fields reflect this role exactly:

- **`sfXChainBridge`** — identifies the specific bridge (locking-chain account, issuing-chain account, and asset pair) this commit targets.
- **`sfDestination`** — the account address to create on the destination chain.
- **`sfAmount`** — the funds being committed; this amount seeds the new account's balance after bridge processing.
- **`sfSignatureReward`** — a separate amount that is distributed to the witness servers that attest to this account creation on the destination chain, incentivizing the off-chain infrastructure.

## Two-Class Design: Immutable Wrapper + Fluent Builder

The file follows the same pattern used across all auto-generated transaction types in this directory: one read-only wrapper class and one builder class. This separation is architecturally deliberate.

`XChainAccountCreateCommit` inherits from `TransactionBase`, which holds a `std::shared_ptr<STTx const>` — the `const` qualifier is key. Once constructed, the underlying transaction bytes are immutable. All four domain getters are marked `[[nodiscard]]` and return typed values directly from the `STTx` via `tx_->at(sfField)`, using the `SF_*` type system to return the correct C++ type for each XRPL field (e.g., `SF_ACCOUNT::type::value_type` resolves to `AccountID`, `SF_AMOUNT::type::value_type` to `STAmount`). This avoids stringly-typed field access while staying close to the underlying serialization layer.

The constructor accepts an existing `std::shared_ptr<STTx const>` — this is the deserialization path. A type guard checks `tx_->getTxnType() != txType` and throws `std::runtime_error` on mismatch. Because the base class constructor runs before the check (consuming the `std::move`), the guard reads from `tx_` (the stored member in `TransactionBase`) rather than the moved-from local. The design is correct: the invariant that a wrapper only ever holds the matching transaction type is enforced at construction time.

`XChainAccountCreateCommitBuilder` inherits from `TransactionBuilderBase<XChainAccountCreateCommitBuilder>` using CRTP, which gives it all common field setters (`setAccount`, `setFee`, `setSequence`, `setLastLedgerSequence`, `setMemos`, etc.) with fluent chaining that returns `Derived&` — the concrete builder type — rather than a base reference, so callers never lose type information mid-chain. The domain-specific setters (`setXChainBridge`, `setDestination`, `setAmount`, `setSignatureReward`) follow the same pattern and accept parameters as `std::decay_t<typename SF_*::type::value_type> const&`, stripping any reference or qualifier from the field's value type to ensure clean value semantics when assigning into the mutable `STObject`.

Internally, `TransactionBuilderBase` stores an `STObject object_{sfTransaction}` — a mutable bag of serialized type fields. Crucially, the base class comment explains why `object_.set(soTemplate)` is deliberately not called: setting a template would create placeholder entries for `soeDEFAULT` fields, which would cause `STTx::applyTemplate()` (called during construction of the final `STTx`) to throw "may not be explicitly set to default." The builder keeps `object_` as a free, template-less `STObject`, relying on the `STTx` constructor to enforce the schema.

## Construction and Signing Flow

The `build(PublicKey, SecretKey)` method finalizes the transaction:

1. Calls `sign()` from `TransactionBuilderBase`, which sets `sfSigningPubKey`, serializes the object prefixed with `HashPrefix::txSign` (excluding signing fields), computes the cryptographic signature, and stores it in `sfTxnSignature`.
2. Constructs a `std::shared_ptr<STTx>` by moving the builder's `object_` into the `STTx` constructor — this triggers schema validation via `applyTemplate()`.
3. Wraps the result in an `XChainAccountCreateCommit` and returns it by value.

The alternative builder constructor — `XChainAccountCreateCommitBuilder(std::shared_ptr<STTx const> tx)` — supports round-tripping an existing transaction back into a builder for modification. It copies the `STTx` into `object_` (`object_ = *tx`) after the same type guard check. This is less common but useful in testing or transaction amendment scenarios.

## Relationship to Sibling Files

The `transactions/` directory contains one header per XRPL transaction type, all generated by the same tool and following identical structural patterns. The XChain-related group — `XChainCreateBridge.h`, `XChainCommit.h`, `XChainClaim.h`, `XChainAddAccountCreateAttestation.h`, and this file — collectively cover the full lifecycle of the cross-chain bridge protocol. `XChainAccountCreateCommit.h` specifically handles the initiating user-side action in the account-creation flow, distinct from `XChainCommit.h` which handles asset transfers for existing accounts.