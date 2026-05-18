# `XChainAddAccountCreateAttestation.h`

This auto-generated file defines the `XChainAddAccountCreateAttestation` transaction type (`ttXCHAIN_ADD_ACCOUNT_CREATE_ATTESTATION`, opcode 46), one of eight cross-chain bridge transaction types gated behind the `featureXChainBridge` amendment. Its specific purpose is to allow a trusted witness server to submit a cryptographic attestation that an account-creation event occurred on a counterpart chain, enabling the XRPL bridge to collectively authorize and fund a new account on the destination chain once a quorum of such attestations is received.

## Role in the Cross-Chain Bridge Protocol

The XChain bridge operates through a quorum model: a set of off-chain witness servers independently observe events on a source chain and each submit an attestation transaction to the destination chain. For account creation events — distinct from ordinary value transfers handled by `XChainAddClaimAttestation` — witnesses submit this transaction type. The distinction matters because creating a new account requires the `createAcct` privilege and involves a monotonically ordered counter (`sfXChainAccountCreateCount`) to prevent replay attacks and enforce processing order across independently submitted attestations.

Each attestation carries the witness's own cryptographic identity (`sfPublicKey`, `sfSignature`) alongside the event details: `sfOtherChainSource` (originating account on the remote chain), `sfAmount` (funds being transferred), `sfDestination` (the new account to create on XRPL), and `sfWasLockingChainSend` (a `uint8` flag distinguishing which side of the bridge — locking or issuing — initiated the send). The `sfXChainBridge` field identifies which bridge configuration governs this attestation.

The separation between `sfAttestationSignerAccount` and `sfAttestationRewardAccount` is a deliberate design choice: the signer account identifies the witness cryptographically, while the reward account is where the protocol pays the `sfSignatureReward` once quorum is reached. These can be different addresses, so witness operators can segregate their signing key from their compensation-receiving account.

## Two-Class Design: Immutable Wrapper and CRTP Builder

The file follows the uniform pattern across all transaction types in this directory, pairing an immutable read accessor class with a mutable builder.

`XChainAddAccountCreateAttestation` inherits from `TransactionBase` and wraps a `shared_ptr<STTx const>`. Its constructor performs a strict transaction-type check, throwing `std::runtime_error` if the wrapped `STTx` is not `ttXCHAIN_ADD_ACCOUNT_CREATE_ATTESTATION`. This fail-fast guard is essential because `STTx` is a generic container and there is no compile-time mechanism to enforce transaction type — without the runtime check, a wrapper could silently misinterpret fields from a different transaction kind. All eleven field accessors are `[[nodiscard]]` and delegate to `tx_->at(sfField)`, which throws if the field is absent; this is sound because the ledger validates required-field presence before these wrappers would normally be instantiated over ingested transactions.

`XChainAddAccountCreateAttestationBuilder` inherits from `TransactionBuilderBase<XChainAddAccountCreateAttestationBuilder>` using CRTP. The base class template uses `static_cast<Derived&>(*this)` in its setters to return the correct derived type for method chaining without virtual dispatch overhead. The builder accumulates fields into a mutable `STObject object_` which is later promoted to a signed `STTx` on `build()`.

## Constructor Completeness Guarantee

The builder's primary constructor requires all eleven transaction-specific fields as non-optional parameters, alongside the submitting `account`, and optional `sequence` and `fee`. Every required field is set in the constructor body before the object is usable. This means a partially-constructed `XChainAddAccountCreateAttestationBuilder` cannot exist in a state where required fields are missing — the only intermediate state is an unsent `STObject` in the builder, not an invalid one. Optional `sequence` and `fee` cover the common cases where callers rely on ledger autofill for sequence management.

The secondary constructor taking `shared_ptr<STTx const>` enables a roundtrip workflow: an existing serialized transaction can be deserialized back into a builder for re-signing or inspection, with the same type guard applied.

## The `build()` Method and Move Semantics

`build(publicKey, secretKey)` invokes `sign()` inherited from `TransactionBuilderBase`, which serializes the `STObject` with `HashPrefix::txSign` prepended, signs the bytes, and sets `sfSigningPubKey` and `sfTxnSignature` in the object. It then move-constructs an `STTx` from the now-owned `object_`, wraps it in a `shared_ptr`, and returns an `XChainAddAccountCreateAttestation` wrapper. The move is intentional: after `build()` the builder's internal state is consumed, making it structurally clear that `build()` is a terminal operation.

## Use of `std::decay_t` in Setter Signatures

All setter parameters are declared as `std::decay_t<typename SF_FIELD::type::value_type> const&`. The `std::decay_t` strips any reference and cv-qualifiers from the field's underlying value type before applying `const&`. This is a defensive pattern: if a field descriptor's `value_type` were itself a reference type, naively adding `const&` could create a reference-to-reference or an unexpected qualified type. Decaying first guarantees the parameter is always a plain const lvalue reference regardless of how the underlying field type is specified.

## Integration with the Auto-Generated Transaction Layer

This file is entirely auto-generated and coexists with 70+ sibling transaction headers in the same directory. Its structural uniformity with files like `XChainAddClaimAttestation.h`, `XChainAccountCreateCommit.h`, and `XChainCreateBridge.h` is deliberate: the entire `xrpl::transactions` namespace provides a consistent, type-safe API surface over raw `STTx` objects without requiring callers to know field names or types at the point of use. The generation approach eliminates per-transaction boilerplate inconsistencies that would otherwise arise from manual authorship of dozens of similar wrappers.