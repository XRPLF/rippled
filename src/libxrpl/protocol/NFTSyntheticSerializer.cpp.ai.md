# `NFTSyntheticSerializer.cpp`

## Role and Purpose

This file defines a single aggregating function, `insertNFTSyntheticInJson`, that sits inside the `xrpl::RPC` namespace and serves as the unified entry point for enriching a transaction JSON response with NFT-related fields that the XRPL ledger does not store directly. These fields are called "synthetic" because they are derived at query time by analyzing transaction metadata rather than being recorded as first-class ledger fields.

The function is called by RPC handlers such as `Tx.cpp` immediately after writing the raw metadata to the response:

```cpp
response[jss::meta] = meta->getJson(JsonOptions::none);
insertDeliveredAmount(response[jss::meta], context, result.txn, *meta);
RPC::insertNFTSyntheticInJson(response, sttx, *meta);
RPC::insertMPTokenIssuanceID(response[jss::meta], sttx, *meta);
```

This placement shows that `insertNFTSyntheticInJson` is part of a post-processing pipeline that layers derived context onto an already-serialized transaction response.

## What It Computes

The function delegates to two independent subsystems:

**`insertNFTokenID`** (from `NFTokenID.cpp`) adds `nftoken_id` or `nftoken_ids` to `response[jss::meta]` for successful `NFTokenMint`, `NFTokenAcceptOffer`, and `NFTokenCancelOffer` transactions. The derivation is non-trivial: because the ledger stores NFTs packed into page objects rather than as individual entries, the function must compare the pre-transaction and post-transaction NFToken arrays across all affected ledger nodes in the metadata to identify which token was created or affected by the operation. For `NFTokenCancelOffer`, it scans deleted `NFTokenOffer` nodes and returns a deduplicated array of affected token IDs.

**`insertNFTokenOfferID`** (from `NFTokenOfferID.cpp`) adds `offer_id` to `response[jss::meta]` for successful `NFTokenCreateOffer` transactions (and `NFTokenMint` transactions that include an `sfAmount` field, i.e., mints that create an immediate sell offer). It locates the newly created `NFTokenOffer` ledger node in the metadata and extracts its `sfLedgerIndex` as the offer identifier.

Both helpers perform their own eligibility check (`canHaveNFTokenID`, `canHaveNFTokenOfferID`) that gates on transaction type and `tesSUCCESS`, meaning failed transactions produce no synthetic output at all.

## Design Rationale

The split into three files — this compositor plus two dedicated modules — reflects deliberate design for reusability. The comment in both `NFTokenID.h` and `NFTokenOfferID.h` explicitly states: *"Helper functions are not static because they can be used by Clio."* Clio is the XRPL History API server, which parses ledger data independently of `rippled`. By keeping the computation logic in header-exposed, non-static functions under `xrpl::` (not `xrpl::RPC::`), those helpers can be called directly by Clio without pulling in the RPC coupling of `insertNFTSyntheticInJson`.

This file therefore plays the role of a composition point: it belongs to `xrpl::RPC` because its job is to mutate a JSON response object intended for external API consumers, while the underlying extraction logic belongs to the broader `xrpl::` namespace where it is accessible to any consumer of the library.

## Input Handling and Safety

The function takes `response` by non-const reference and writes into `response[jss::meta]`, which `Json::Value` creates on demand if absent. The `transaction` parameter is a `std::shared_ptr<STTx const>`, and both delegate functions guard against a null pointer at their first line. The `transactionMeta` is passed by const reference, providing safe read-only access. There is no explicit error handling at this layer because both delegates are designed to be no-ops when they cannot produce a meaningful result — they return early rather than throwing.

As a result, `insertNFTSyntheticInJson` itself is unconditionally safe to call for any transaction type: non-NFT transactions simply produce no output because all eligibility checks inside the delegates will fail, and the response object is left unmodified for those fields.