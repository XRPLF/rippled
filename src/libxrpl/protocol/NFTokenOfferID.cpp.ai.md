# `NFTokenOfferID.cpp` — Synthetic Offer ID Injection for NFToken Transactions

This file provides three utility functions that together inject a synthetic `offer_id` field into RPC JSON responses for transactions that create NFToken offers. The ID is not stored in the transaction itself — it is derived at query time by scanning the transaction's metadata — making these functions essential for API consumers who need to know which ledger object was created without having to manually parse the affected-nodes list.

## Why Synthetic Fields Exist

The XRPL canonical transaction format records only the inputs to a transaction, not the outputs. When a transaction creates a new ledger object, the key of that object is determined during consensus processing and encoded only in the transaction's metadata (`TxMeta`). For NFToken offers specifically, clients need the offer's ledger index (its `uint256` key) to subsequently accept or cancel it. Rather than requiring every API consumer to walk the `AffectedNodes` array themselves, `insertNFTokenOfferID` does this extraction and attaches the result as `offer_id` inside the `meta` JSON object.

The header comment is explicit that the helper functions are deliberately non-static so that **Clio** — a separate read-optimized XRP Ledger data server — can reuse them directly. Static linkage would break that use case; this is a cross-component design constraint captured in the interface documentation.

## Transaction Eligibility: `canHaveNFTokenOfferID`

The guard function enforces three conditions in sequence, returning `false` immediately on any failure:

1. **Null pointer check** — the `serializedTx` `shared_ptr` is tested before any dereference. This is the only defensive null check needed because `shared_ptr` construction does not guarantee a non-null stored pointer.
2. **Transaction type check** — only `ttNFTOKEN_CREATE_OFFER` always qualifies. `ttNFTOKEN_MINT` qualifies only when `sfAmount` is present, because that field signals that the mint transaction also creates a buy offer for immediate sale. Other mint transactions do not create an offer object and thus can never have an `offer_id`.
3. **Success check** — `isTesSuccess(transactionMeta.getResultTER())` filters out failed transactions. A failed transaction never modifies the ledger, so no offer object could have been created regardless of type.

The combination of these checks makes the subsequent metadata scan safe and avoids wasted work on the vast majority of transactions.

## Metadata Extraction: `getOfferIDFromCreatedOffer`

This function iterates `transactionMeta.getNodes()` — the `STArray` of affected ledger nodes — and looks for the single node that:

- Has `sfLedgerEntryType == ltNFTOKEN_OFFER` (confirming it is an NFToken offer object, not a different ledger entry type such as an account root or directory node), and
- Has its field name equal to `sfCreatedNode` (confirming the node was freshly created by this transaction, not modified or deleted).

When found, the function returns `node.getFieldH256(sfLedgerIndex)` — the node's ledger key, which serves as the globally unique offer ID. The function returns `std::nullopt` if no qualifying node is found, which can happen legitimately even on an ostensibly eligible transaction (e.g., edge cases where metadata is absent or the offer creation path was not taken).

Returning `std::optional<uint256>` rather than throwing is consistent with the overall XRPL error-handling philosophy: protocol-level code avoids exceptions and signals absence through value types.

## Integration Point: `insertNFTokenOfferID`

The public-facing function composes the two helpers: it calls `canHaveNFTokenOfferID` as a fast pre-filter, then calls `getOfferIDFromCreatedOffer` and, if a value is present, inserts it into the `Json::Value` response under `jss::offer_id` as a hex string. The function is a no-op when any check fails — it never throws and never modifies the response if the offer ID cannot be determined.

In practice this function is called from `insertNFTSyntheticInJson` in `NFTSyntheticSerializer.cpp`, which is the single entry point for adding all NFT-related synthetic fields to a transaction's `meta` JSON object. That wrapper also calls `insertNFTokenID` for the NFToken mint ID, so both synthetic enrichments follow the same pattern and share the same call site.

## Key Design Observations

The offer ID is recovered from `sfLedgerIndex` on the `CreatedNode`, not from a dedicated field on the transaction. This is intentional: the ledger index of an object is its canonical identifier, and reusing it avoids introducing a redundant field in the transaction format. The approach is also exact — there can be at most one `CreatedNode` of type `ltNFTOKEN_OFFER` per transaction, so the loop exits on the first match without ambiguity.

The separation into three functions (guard, extractor, inserter) rather than a monolithic implementation ensures that `getOfferIDFromCreatedOffer` can be called independently by external consumers like Clio that may have already performed their own eligibility checks through different means.