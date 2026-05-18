# `NFTokenOfferID.h` — Synthetic Offer ID Injection for NFTokenCreateOffer

This header declares three free functions that collectively solve a metadata-enrichment problem: when a client submits an `NFTokenCreateOffer` transaction (or an `NFTokenMint` that carries an embedded offer via `sfAmount`), the raw transaction record does not include the resulting offer's ledger index. That ID must be recovered by scanning the transaction's affected-node metadata and injected into the RPC response as a synthetic `offer_id` field.

## The Problem Being Solved

On the XRPL, a newly-created `NFTokenOffer` ledger object is identified by its `LedgerIndex` (a `uint256` hash). This value is deterministic but is not echoed back in the transaction's canonical fields — it only appears implicitly in the `CreatedNode` entries of the transaction metadata. Without a helper like this, callers (wallet software, indexers, explorers) would have to traverse the entire `AffectedNodes` array themselves to find the offer they just created. The functions here encapsulate that scan once, in a reusable way.

## Function Responsibilities

`canHaveNFTokenOfferID` is the guard predicate. It first rejects null transactions, then filters to only the two transaction types that can create an `NFTokenOffer` object — `ttNFTOKEN_CREATE_OFFER` unconditionally, and `ttNFTOKEN_MINT` only when the `sfAmount` field is present (the mint-with-offer variant). Any transaction that failed (`!isTesSuccess`) is also rejected immediately. This tight pre-check prevents unnecessary metadata traversal and also ensures the `offer_id` field is never emitted for failed transactions where no object was created.

`getOfferIDFromCreatedOffer` performs the actual extraction. It iterates over the transaction's affected nodes (`TxMeta::getNodes()`), skipping any node that is not of type `ltNFTOKEN_OFFER` or not a `sfCreatedNode` (i.e., modified or deleted nodes are ignored). The first qualifying node's `sfLedgerIndex` is returned as a `std::optional<uint256>`. The `optional` return is meaningful: even after passing the `canHaveNFTokenOfferID` check there is a theoretical path where no `CreatedNode` exists (e.g., corrupt metadata), so callers must handle absence.

`insertNFTokenOfferID` is the orchestrating entry point. It delegates to both of the above — short-circuiting via `canHaveNFTokenOfferID`, then conditionally writing the `offer_id` string into the `Json::Value` response when extraction succeeds. This is the only function typically called by higher-level code.

## Architectural Position

The comment in the header explicitly notes that these functions are *not* static "because they can be used by Clio." Clio is the separate read-optimized XRPL data API that consumes the same core library without running a full validator node. Making the helpers free functions in `libxrpl` (rather than hidden inside an RPC handler) allows Clio to call them directly without duplicating the logic.

The actual call site in the rippled RPC layer is `insertNFTSyntheticInJson` in `NFTSyntheticSerializer.cpp`, which invokes both this and the parallel `insertNFTokenID` (from `NFTokenID.h`) back-to-back on `response[jss::meta]`:

```cpp
insertNFTokenID(response[jss::meta], transaction, transactionMeta);
insertNFTokenOfferID(response[jss::meta], transaction, transactionMeta);
```

This sibling relationship with `NFTokenID.h` is deliberate: `NFTokenID.h` handles the analogous injection of `nftoken_ids` for mint/accept/cancel operations, while `NFTokenOfferID.h` handles `offer_id` for create-offer operations. Both follow the same three-function pattern (guard → extract → insert) and both enrich the `meta` sub-object of the JSON response rather than the top-level transaction fields.

## Design Notes

The use of `std::shared_ptr<STTx const>` (rather than a raw reference) for the transaction parameter reflects the ownership model of the broader RPC layer, where deserialized transactions are reference-counted. Passing `TxMeta` by const reference is appropriate since metadata is read-only here. The `optional` on `getOfferIDFromCreatedOffer` is preferable to an exception-based failure because missing metadata is a plausible but non-exceptional condition when processing historical or externally-sourced transactions.