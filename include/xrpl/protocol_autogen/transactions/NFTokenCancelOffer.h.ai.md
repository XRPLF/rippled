# `NFTokenCancelOffer.h` — Auto-generated Transaction Wrapper

## Role and Context

This header is part of the `protocol_autogen` layer in `xrpl::transactions`, a set of files generated from the XRPL transaction schema rather than hand-written. It defines two complementary classes for the `NFTokenCancelOffer` transaction type (`ttNFTOKEN_CANCEL_OFFER`, code 28): an immutable read-only wrapper and a mutable builder. This file is one of ~60 analogous files, each following the same structural pattern, located under `include/xrpl/protocol_autogen/transactions/`.

An `NFTokenCancelOffer` transaction on the XRP Ledger removes one or more pending NFT buy or sell offer objects from the ledger. The submitting account does not need to own the offers; any account may cancel any offer, though the most common use is an offer creator cancelling their own open offers.

## Class: `NFTokenCancelOffer`

`NFTokenCancelOffer` extends `TransactionBase`, which wraps a `std::shared_ptr<STTx const>` — the canonical immutable XRPL serialized transaction type. The const qualifier on the shared pointer makes the entire class a read-only view: once constructed, no fields can be modified.

Construction takes a `shared_ptr<STTx const>` and immediately verifies the transaction type via `tx_->getTxnType() != txType`. If the wrong transaction type is passed (for example, an `NFTokenCreateOffer` transaction accidentally routed to this constructor), a `std::runtime_error` is thrown at construction time rather than silently returning garbage from field accessors later. This fail-fast design keeps bugs close to their source.

The only transaction-specific accessor is `getNFTokenOffers()`, which returns `SF_VECTOR256::type::value_type` — that is, a `std::vector<uint256>`. Each `uint256` in the vector is the ledger object ID (key hash) of an `NFTokenOffer` ledger entry to be deleted. The field is `soeREQUIRED`, so no optional wrapper is needed; the underlying `STTx::at()` call will throw if the field is somehow absent, which should only happen if an improperly constructed transaction bypasses the builder.

Common transaction fields (account, fee, sequence, signers, memos, etc.) are all inherited from `TransactionBase` and are not redeclared here.

## Class: `NFTokenCancelOfferBuilder`

`NFTokenCancelOfferBuilder` extends `TransactionBuilderBase<NFTokenCancelOfferBuilder>` using CRTP (Curiously Recurring Template Pattern). This lets the base-class common setters — `setAccount()`, `setFee()`, `setFlags()`, `setLastLedgerSequence()`, and so on — return `Derived&` (i.e., `NFTokenCancelOfferBuilder&`) rather than `TransactionBuilderBase&`, preserving the fluent method-chaining interface across both base and derived setters without virtual dispatch overhead.

The primary constructor accepts `account`, `nFTokenOffers`, and optionally `sequence` and `fee`. The `nFTokenOffers` argument uses `std::decay_t<typename SF_VECTOR256::type::value_type>` — which strips references to avoid binding to temporaries — then passes `const&`. This is the standard pattern used across all vector-typed fields in this codebase. The constructor delegates to `TransactionBuilderBase`'s constructor to set the transaction type and account, then calls `setNFTokenOffers()`.

A second constructor accepts an `std::shared_ptr<STTx const>`, copies the existing signed transaction into the internal `object_` (`STObject`), and allows re-editing. This path is guarded with the same type check. This is useful when deserializing a transaction from the wire or ledger and needing to produce a modified variant.

The `build()` method is the terminal step: it calls the protected `sign()` helper from `TransactionBuilderBase`, which serializes the object with `HashPrefix::txSign` prepended (the XRPL signing prefix), computes the signature using the provided `PublicKey`/`SecretKey`, embeds the signature and public key into `object_`, then constructs and returns an `NFTokenCancelOffer` from a newly heap-allocated `STTx` that takes ownership of `object_` via `std::move`. After `build()` returns, `object_` is in a moved-from state; the builder must not be reused.

## Design Observations

This class pair illustrates a clean separation between construction-time mutability and runtime immutability. The builder owns a plain `STObject` (not yet a full `STTx`) to avoid the constraints that `STTx::applyTemplate()` would impose on unset default fields. Only at `build()` time does the `STObject` become a fully validated `STTx`. This avoids a subtle bug: calling `STTx` template application on an incomplete object would throw for fields that are `soeDEFAULT` but not explicitly set — a non-obvious failure mode that the comment in `TransactionBuilderBase`'s constructor explicitly calls out.

The `NFTokenCancelOffer` transaction is marked as `Delegation::delegable` in its schema metadata, meaning the `sfDelegate` optional field (defined in `TransactionBase`) may be set, allowing a delegate account to submit this transaction on behalf of the originating account — a capability surfaced via `TransactionBase::setDelegate()` and `getDelegate()`.

Because `sfNFTokenOffers` carries a vector of `uint256` IDs rather than a single ID, a single `NFTokenCancelOffer` can batch-cancel many offers in one ledger transaction, avoiding the fee overhead of submitting individual cancellations. The minimum meaningful content of this transaction is therefore one entry in the offers vector; the protocol enforces that the list is non-empty.