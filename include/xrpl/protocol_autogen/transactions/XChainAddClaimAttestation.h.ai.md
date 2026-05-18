# `XChainAddClaimAttestation.h` — Cross-Chain Transfer Attestation Transaction

This file is auto-generated (marked with `// This file is auto-generated. Do not edit.`) and lives inside the `xrpl/protocol_autogen/transactions/` collection, which provides one header per XRPL transaction type. It implements transaction type `ttXCHAIN_ADD_CLAIM_ATTESTATION` (ordinal 45), introduced under the `featureXChainBridge` amendment. Its purpose is to model the message that a witness server submits to the XRPL to attest that a `XChainCommit` occurred on the paired chain.

## Protocol Context

The XChain bridge protocol requires a set of trusted external observers — called witness servers — to monitor both chains and relay proof of transfers between them. The workflow for a regular cross-chain value transfer proceeds as:

1. A user submits `XChainCreateClaimID` on the destination chain to reserve a claim slot.
2. The user submits `XChainCommit` on the source chain, locking or burning assets.
3. Each witness server that sees the commit submits `XChainAddClaimAttestation` on the destination chain.
4. Once a quorum of attestations accumulates, the user (or the last attesting witness, if the destination was pre-specified) can trigger `XChainClaim` to release the funds.

`XChainAddClaimAttestation` is thus the mechanism by which decentralized off-chain evidence is anchored on-chain. Each submission carries a cryptographic signature from the witness over the claim details, letting the ledger independently verify each attestor's commitment without trusting any central coordinator.

## The Immutable Wrapper: `XChainAddClaimAttestation`

`XChainAddClaimAttestation` inherits from `TransactionBase`, which wraps a `std::shared_ptr<STTx const>`. The wrapper is intentionally read-only — all fields are exposed through `[[nodiscard]]` const getters, and `tx_` is a `const` smart pointer. This prevents accidental mutation of a transaction after it has been submitted or retrieved from the ledger.

The constructor validates the transaction type at runtime:

```cpp
if (tx_->getTxnType() != txType)
    throw std::runtime_error("Invalid transaction type for XChainAddClaimAttestation");
```

The static `txType` constexpr member allows compile-time dispatch in template code (e.g. transaction visitors), while the runtime check guards against wrapping a wrong `STTx` object — a defensive pattern used uniformly across all autogen transaction types.

### Field Access

Nine fields are marked `soeREQUIRED` and return their native value types directly via `tx_->at(sf...)`:

| Getter | Field | Type |
|---|---|---|
| `getXChainBridge()` | `sfXChainBridge` | `SF_XCHAIN_BRIDGE::type::value_type` |
| `getAttestationSignerAccount()` | `sfAttestationSignerAccount` | `AccountID` |
| `getPublicKey()` | `sfPublicKey` | `Blob` (VL) |
| `getSignature()` | `sfSignature` | `Blob` (VL) |
| `getOtherChainSource()` | `sfOtherChainSource` | `AccountID` |
| `getAmount()` | `sfAmount` | `STAmount` |
| `getAttestationRewardAccount()` | `sfAttestationRewardAccount` | `AccountID` |
| `getWasLockingChainSend()` | `sfWasLockingChainSend` | `uint8_t` |
| `getXChainClaimID()` | `sfXChainClaimID` | `uint64_t` |

The single optional field, `sfDestination`, uses `protocol_autogen::Optional<T>` — a type alias defined in `Utils.h`. Because `std::optional` cannot hold references, `Optional<T>` uses `std::conditional_t` to substitute `std::optional<std::reference_wrapper<std::remove_reference_t<T>>>` when `T` is a reference type, and `std::optional<T>` otherwise. The pattern `hasDestination()` / `getDestination()` is consistent throughout the autogen layer: the presence check is separated from the accessor so callers can conditionally access without catching exceptions.

`sfDestination` being optional reflects the protocol: the committing user on the source chain may pre-specify where funds land, or they may defer that to a later `XChainClaim`. Witness servers record whatever the commit stated.

## The Builder: `XChainAddClaimAttestationBuilder`

`XChainAddClaimAttestationBuilder` uses the Curiously Recurring Template Pattern (CRTP) through `TransactionBuilderBase<XChainAddClaimAttestationBuilder>`. Every setter in the base class and the derived class returns `Derived&` (i.e. `XChainAddClaimAttestationBuilder&`), enabling method chaining without repeated casts.

The CRTP base (`TransactionBuilderBase`) holds the mutable `STObject object_{sfTransaction}` that accumulates field assignments before the transaction is finalized. Critically, the base class comment explains why it avoids calling `object_.set(soTemplate)`: doing so would create `STBase` placeholders for `soeDEFAULT` fields, which would later cause `applyTemplate()` to throw "may not be explicitly set to default" when the `STTx` constructor processes the object. This is a subtle interaction between the XRPL serialization type system and the autogen layer.

The constructor enforces that all nine required fields are set at construction time, passing them as arguments rather than allowing them to be added lazily. Optional `sequence` and `fee` are `std::optional` parameters with `std::nullopt` defaults, because test or simulation environments sometimes omit these for later injection.

A second constructor accepts an existing `std::shared_ptr<STTx const>` and copies the raw `STObject`, allowing round-trip mutation: deserialize a transaction, wrap it in a builder, modify a field, re-sign, and rebuild.

The terminal operation is `build(PublicKey, SecretKey)`, which delegates to `TransactionBuilderBase::sign()`. That method serializes the object without signing fields (`addWithoutSigningFields`), prepends `HashPrefix::txSign`, signs with the provided key pair, stores `sfSigningPubKey` and `sfTxnSignature`, then constructs a `shared_ptr<STTx>` and wraps it in a fresh immutable `XChainAddClaimAttestation`. After `build()`, the builder's internal state has been moved out; it should not be reused.

## Relationship to Sibling Transactions

The sister type `XChainAddAccountCreateAttestation` (type 46) serves the same attestation role but for `XChainAccountCreateCommit`, which creates a brand-new account on the destination chain instead of crediting an existing one. Both share the same structural pattern — the difference lies in their required fields. Together they cover the two ways a cross-chain transfer can complete.

## Design Rationale

Generating these files rather than hand-authoring them eliminates the class of bugs where a field is added to the protocol definition but forgotten in the C++ accessor layer. Every field declared `soeREQUIRED` gets a getter that returns the value directly; every `soeOPTIONAL` field gets a `has*()` / `get*()` pair returning `Optional<T>`. The separation of immutable reader (`XChainAddClaimAttestation`) from mutable builder (`XChainAddClaimAttestationBuilder`) enforces the invariant that once a transaction exists as a signed `STTx`, its contents cannot change — matching the ledger's own semantics.