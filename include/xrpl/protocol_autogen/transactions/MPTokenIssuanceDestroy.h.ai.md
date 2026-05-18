# `MPTokenIssuanceDestroy.h`

Auto-generated header in `xrpl::transactions` that defines the type-safe C++ interface for the `MPTokenIssuanceDestroy` transaction — the ledger operation that permanently removes a Multi-Purpose Token (MPT) issuance object from the XRP Ledger. This file is one entry in a large family of per-transaction-type headers under `include/xrpl/protocol_autogen/transactions/`, each produced by the same code generator to guarantee uniform structure across all transaction kinds.

## Role in the MPT Lifecycle

MPT issuances are created by `MPTokenIssuanceCreate` (type 54, `featureMPTokensV1`). Once an issuer has wound down all outstanding balances and holder positions, they submit an `MPTokenIssuanceDestroy` transaction (type 55) to reclaim the on-ledger reserve the `MPTokenIssuance` object occupies. The sole required field — `sfMPTokenIssuanceID` — encodes which issuance to delete. This identifier is a 192-bit (`SF_UINT192`) value derived from the issuer's account and the sequence number of the originating `MPTokenIssuanceCreate`, making it globally unique and non-colliding. The transaction is gated behind the `featureMPTokensV1` amendment and requires the `destroyMPTIssuance` privilege, and it is marked `Delegation::delegable`, meaning the `sfDelegate` field (inherited from `TransactionBase`) may be used to allow a third-party account to submit the transaction on the issuer's behalf.

## Two-Class Design: Wrapper and Builder

The file follows the split pattern established by `TransactionBase` and `TransactionBuilderBase`. The rationale is strict separation between construction (mutable) and observation (immutable after signing).

**`MPTokenIssuanceDestroy`** is the immutable reader. It inherits from `TransactionBase`, which holds a `std::shared_ptr<STTx const>` and exposes typed getters for all standard fields (account, sequence, fee, flags, memos, signers, delegate, and more). `MPTokenIssuanceDestroy` adds exactly one transaction-specific getter:

```cpp
[[nodiscard]]
SF_UINT192::type::value_type
getMPTokenIssuanceID() const;
```

This unconditionally dereferences `sfMPTokenIssuanceID` via `tx_->at(...)`, which is safe because the field is `soeREQUIRED` in the transaction schema — it must be present in any well-formed `STTx` of this type. The constructor enforces the type precondition by comparing `tx_->getTxnType()` against the `txType` constant (`ttMPTOKEN_ISSUANCE_DESTROY`) and throwing `std::runtime_error` on mismatch, preventing a caller from accidentally wrapping a `Payment` or other unrelated transaction in this class.

**`MPTokenIssuanceDestroyBuilder`** is the mutable side. It inherits from `TransactionBuilderBase<MPTokenIssuanceDestroyBuilder>` — a CRTP template that returns `Derived&` from every setter, enabling fluent method-chaining across all common fields (`setFee`, `setSequence`, `setLastLedgerSequence`, `setDelegate`, etc.). The builder stores its state in a plain `STObject object_` (not yet an `STTx`) to avoid triggering `applyTemplate()` validation prematurely; the `STTx` is only instantiated at `build()` time.

The builder offers two construction paths:
- **From scratch**: takes a required `account` and the mandatory `mPTokenIssuanceID`, plus optional `sequence` and `fee`. The required field is immediately written into `object_` via `setMPTokenIssuanceID()`, enforcing the invariant that it is always present before signing.
- **From an existing `STTx`**: copies the object data out of a previously-deserialized transaction, after verifying the transaction type. This supports round-trip use cases where a partially-constructed or received transaction needs to be re-signed or modified before re-submission.

The `build()` method calls the protected `sign()` helper (from `TransactionBuilderBase`), which serializes the object with `HashPrefix::txSign` prepended, signs it with the provided key pair, sets `sfSigningPubKey` and `sfTxnSignature`, then promotes `object_` into an `STTx` via `std::make_shared<STTx>(std::move(object_))` and wraps it in the immutable `MPTokenIssuanceDestroy` reader.

## Relationship to Sibling Files

By comparison with `MPTokenIssuanceCreate.h`, the destroy variant is notably sparse: `MPTokenIssuanceCreate` carries six optional fields (`sfAssetScale`, `sfTransferFee`, `sfMaximumAmount`, `sfMPTokenMetadata`, `sfDomainID`, `sfMutableFlags`) that configure the issuance at birth, whereas `MPTokenIssuanceDestroy` carries only the single required identifier used to look up and delete the object. This asymmetry reflects the protocol: creation is rich and configurable, destruction is a targeted removal keyed by ID.

The `static constexpr xrpl::TxType txType = ttMPTOKEN_ISSUANCE_DESTROY` member on the wrapper class allows callers to dispatch or introspect the type at compile time without instantiating an object, consistent with every other auto-generated transaction wrapper in this directory.