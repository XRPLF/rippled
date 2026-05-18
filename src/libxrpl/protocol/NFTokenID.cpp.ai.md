# `src/libxrpl/protocol/NFTokenID.cpp`

## Purpose

This file solves a specific gap in how the XRPL ledger records NFT transactions: the raw transaction metadata does not directly identify which NFT was minted, traded, or involved in a cancelled offer. It records ledger state changes — what the affected `NFTokenPage` objects looked like before and after the transaction — but does not annotate "this is the new token." The functions here bridge that gap by reconstructing the NFToken identity from first principles, then injecting it into the JSON response so that API consumers don't have to perform the same inference themselves.

The public header explicitly notes that the helper functions are not `static` because they are also consumed by **Clio**, the external XRPL history node, which performs the same enrichment independently of `rippled`. This is a deliberate API boundary, not just internal decomposition.

## Core Problem: The Missing Token ID

When `ttNFTOKEN_MINT` succeeds, it adds one entry to a `NFTokenPage` ledger object. The transaction metadata records what the page's token list looked like in `sfPreviousFields` and `sfFinalFields`, but both contain every token in the page, not just the new one. The ledger format does not mark the inserted token explicitly.

`getNFTokenIDFromPage()` resolves this with a set-difference approach. It iterates every metadata node, collecting all `uint256` token IDs from previous states into `prevIDs` and from final states into `finalIDs`. After the loop it asserts the invariant: `finalIDs.size() == prevIDs.size() + 1`. If that doesn't hold — meaning something unexpected happened — the function returns `std::nullopt` rather than guessing. When the sizes do match, `std::mismatch` finds the first position where the sorted-by-construction sequences diverge; the entry in `finalIDs` at that position is the newly minted token.

There is a subtle edge case handled inside the loop: when a mint causes an existing page to split, the resulting linked-list rewiring may produce a `sfModifiedNode` for a third page whose `sfPreviousFields` doesn't include `sfNFTokens` at all — only its `NextPageMin` or `PreviousPageMin` pointers changed. The code guards against this with `previousFields.isFieldPresent(sfNFTokens)` before attempting to extract the token array, skipping such nodes silently. Without this guard, the size invariant check would incorrectly fail and return `std::nullopt` for legitimate mints.

## Offer-Based Extraction

For `ttNFTOKEN_ACCEPT_OFFER` and `ttNFTOKEN_CANCEL_OFFER`, the token identity is recoverable more directly. Both transaction types delete `ltNFTOKEN_OFFER` ledger objects, and each offer's `sfFinalFields` carries the `sfNFTokenID` it was created for. `getNFTokenIDFromDeletedOffer()` scans all metadata nodes for `sfDeletedNode` entries of type `ltNFTOKEN_OFFER` and collects those token IDs.

The return type differs between the two call sites. `ttNFTOKEN_CANCEL_OFFER` can cancel many offers simultaneously, and multiple offers can reference the same NFT, so the function deduplicates with `sort` + `unique` + `erase` and returns a `std::vector<uint256>`. `ttNFTOKEN_ACCEPT_OFFER` accepts exactly one offer, so `insertNFTokenID` uses only `result.front()`. The JSON output reflects this: mint and accept-offer inject a single `jss::nftoken_id` string, while cancel-offer injects a `jss::nftoken_ids` array.

## Entry Point and Callsite

`insertNFTokenID()` is the public entry point that ties everything together. It begins with `canHaveNFTokenID()`, which gates on three conditions: the transaction pointer is non-null, the transaction type is one of the three NFT types, and `isTesSuccess(transactionMeta.getResultTER())` is true. A failed transaction cannot have added or removed an NFT, so early return avoids spurious metadata diffs.

In practice, `insertNFTokenID` is called from `insertNFTSyntheticInJson()` in `NFTSyntheticSerializer.cpp`, which also calls the sibling `insertNFTokenOfferID()`. Together they form the "synthetic" enrichment layer — fields that are added to the API response derived from metadata rather than existing directly in ledger objects. The JSON path they target is `response[jss::meta]`, meaning the enriched fields appear inside the `meta` sub-object of a transaction response.

## Error Handling and Defensive Patterns

All failure modes in the metadata-parsing functions produce graceful no-ops rather than exceptions. `getNFTokenIDFromPage()` returns `std::nullopt` if the size invariant is violated or if `std::mismatch` unexpectedly reaches the end of `finalIDs`. `insertNFTokenID()` simply omits the field from the response if no result is found; callers receive a valid but unenriched JSON object. No exceptions are thrown anywhere in this file. The `downcast<STObject>()` calls on `sfNewFields`, `sfPreviousFields`, and `sfFinalFields` rely on the XRPL serialized-type system to enforce structural correctness; malformed metadata would throw at that layer, not here.