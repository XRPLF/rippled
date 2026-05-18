# `include/xrpl/protocol/NFTSyntheticSerializer.h`

## Role

This header declares a single aggregator function, `insertNFTSyntheticInJson`, that injects "synthetic" NFT-related fields into transaction RPC responses. The word *synthetic* is deliberate: these fields — `nftoken_ids` and `offer_id` — are not stored anywhere on-chain. They must be reconstructed at query time by examining the ledger state changes recorded in `TxMeta`. The function exists so that API consumers don't have to parse raw metadata diffs themselves.

## What It Wraps

The implementation (in `src/libxrpl/protocol/NFTSyntheticSerializer.cpp`) is a thin two-line aggregator:

```cpp
insertNFTokenID(response[jss::meta], transaction, transactionMeta);
insertNFTokenOfferID(response[jss::meta], transaction, transactionMeta);
```

`insertNFTokenID` (declared in `NFTokenID.h`) adds a `nftoken_ids` array to the meta block for successful `NFTokenMint`, `NFTokenAcceptOffer`, and `NFTokenCancelOffer` transactions by walking the metadata's modified ledger nodes. `insertNFTokenOfferID` (declared in `NFTokenOfferID.h`) similarly adds an `offer_id` field for successful `NFTokenCreateOffer` transactions.

Both underlying inserters are non-static, explicitly to allow reuse by **Clio**, Ripple's secondary API server — a design constraint called out in the source comments.

## Call Sites

`insertNFTSyntheticInJson` is invoked in three RPC response paths — `Tx.cpp`, `NetworkOPs.cpp`, and `AccountTx.cpp` — always as part of a consistent metadata enrichment sequence alongside `insertDeliveredAmount` and `insertMPTokenIssuanceID`. The same enrichment block is also used in `Simulate.cpp`, so simulated transaction responses are structurally identical to real submission responses.

## Why This File Exists

The aggregator pattern avoids callers needing to remember the individual NFT inserter pair. As new NFT transaction types were added (mint, accept, cancel, create offer), bundling them here prevents each call site from accumulating an ever-growing list of per-type injector calls. The companion `MPTokenIssuanceID` injector follows the same convention but lives in the `xrpld` layer since it is not consumed by Clio.