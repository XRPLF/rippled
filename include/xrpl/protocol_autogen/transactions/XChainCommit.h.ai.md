# `XChainCommit.h` — Auto-Generated Cross-Chain Commit Transaction

## Role in the System

`XChainCommit.h` is an auto-generated header (the `// This file is auto-generated. Do not edit.` marker makes this explicit) that defines the C++ interface for the `XChainCommit` transaction type (`ttXCHAIN_COMMIT`, numeric type 42). It lives in the `xrpl::transactions` namespace alongside every other transaction type in the `protocol_autogen/transactions/` directory, which contains one file per XRPL transaction type.

In the cross-chain bridge protocol (gated behind the `featureXChainBridge` amendment), committing is the first on-chain step of a cross-chain value transfer. A sender on the source chain calls `XChainCommit` to lock funds against a pre-existing `XChainClaimID`. Witnesses observe this and attest to the locking. The recipient then redeems the value on the destination chain via `XChainClaim`. This file provides the C++ typed wrapper and construction machinery for that commit step.

## Two-Class Pattern: Reader and Builder

The file follows the same structural contract as every other transaction in this directory: a paired *immutable wrapper* class and a *builder* class, here `XChainCommit` and `XChainCommitBuilder`.

`XChainCommit` extends `TransactionBase`, which stores a `std::shared_ptr<STTx const>` — a reference-counted, deep-const pointer to the underlying serialized transaction object. Constructing an `XChainCommit` from a shared pointer performs an immediate type guard: `tx_->getTxnType() != txType` throws `std::runtime_error`. This means invalid casts from the untyped `STTx` world are caught at construction time rather than silently yielding garbage from field lookups. The static member `txType = ttXCHAIN_COMMIT` serves as a compile-time constant for this check and for external callers who need to dispatch on type.

All getters are marked `[[nodiscard]]` and `const`, reinforcing the immutability contract. There is no mutation path on `XChainCommit` itself — you cannot partially update a wrapped transaction.

`XChainCommitBuilder` extends `TransactionBuilderBase<XChainCommitBuilder>`, a CRTP base that owns a mutable `STObject object_{sfTransaction}` and exposes fluent setters for common fields like `setAccount()`, `setFee()`, `setSequence()`, and `setLastLedgerSequence()`. Every setter in the CRTP base returns `Derived&` — i.e., `XChainCommitBuilder&` — enabling method chaining across both base and derived setters without casting. The `XChainCommitBuilder`-specific setters (`setXChainBridge`, `setXChainClaimID`, `setAmount`, `setOtherChainDestination`) follow the same pattern, all returning `XChainCommitBuilder&`.

## Fields and Their Design

The transaction has three required fields and one optional field:

- **`sfXChainBridge`** (`SF_XCHAIN_BRIDGE`) — identifies the bridge ledger object defining the two-chain topology (issuing account, door accounts, currency on each side). Required.
- **`sfXChainClaimID`** (`SF_UINT64`) — a 64-bit counter identifying the cross-chain claim ID that was previously created via `XChainCreateClaimID`. This serializes the commit to a specific transfer slot. Required.
- **`sfAmount`** (`SF_AMOUNT`) — the XRP drops or IOU amount being committed and locked. Must match what the bridge was configured to transfer. Required.
- **`sfOtherChainDestination`** (`SF_ACCOUNT`) — the destination account on the other chain. Optional: if omitted, the destination recorded in the claim ID is used. When present, it can override the initially specified recipient.

The optional field handling is idiomatic: `getOtherChainDestination()` delegates to `hasOtherChainDestination()` before calling `tx_->at(sfOtherChainDestination)`, and returns `protocol_autogen::Optional<SF_ACCOUNT::type::value_type>`, which aliases to `std::optional`. This avoids the silent default-value trap that `STObject::at()` would introduce for absent fields.

Setter parameters uniformly use `std::decay_t<typename SFType::type::value_type> const&`. The `std::decay_t` strips reference and cv-qualifiers from the field's canonical `value_type`, so the setters accept plain values (e.g., `AccountID`, `STAmount`, `STXChainBridge`) by const reference regardless of how those value types are declared inside the `SF_*` template hierarchy.

## Construction and Signing

The builder's primary constructor requires all three mandatory fields plus `account`, while `sequence` and `fee` are `std::optional` — a deliberate design choice because these fields are often filled by the calling library (e.g., after fetching the current account sequence from the ledger) rather than known at construction time.

A secondary constructor accepts `std::shared_ptr<STTx const>` directly. This round-trip path is useful when a transaction was deserialized from the wire or loaded from a ledger and the caller wants to modify it before re-signing. The builder copies the `STTx` into its mutable `object_` with `object_ = *tx`.

The `build()` method calls `sign()` from `TransactionBuilderBase`, which serializes the object without signing fields, prepends the `HashPrefix::txSign` prefix, signs it with the provided key pair, and embeds the resulting signature in `sfTxnSignature`. It then wraps the resulting `STObject` in a new `STTx` and hands it to the `XChainCommit` constructor — producing the final immutable wrapper.

## Relationship to Sibling Files

`XChainCommit.h` is one of eight cross-chain bridge transaction files in this directory (`XChainCreateBridge`, `XChainModifyBridge`, `XChainCreateClaimID`, `XChainCommit`, `XChainClaim`, `XChainAccountCreateCommit`, `XChainAddClaimAttestation`, `XChainAddAccountCreateAttestation`). `XChainClaim` is structurally very similar — same required field set minus `OtherChainDestination`, plus a `Destination` field — reflecting the second half of the two-step cross-chain transfer. The autogenerated uniformity across all these files means any tooling that processes the base classes operates correctly for all transaction types without per-type special casing.