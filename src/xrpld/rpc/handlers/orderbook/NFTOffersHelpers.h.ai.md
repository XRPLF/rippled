# `NFTOffersHelpers.h` — Shared Enumeration Logic for NFT Offer RPC Handlers

This header-only module lives in `src/xrpld/rpc/handlers/orderbook/` and provides the shared implementation behind the `nft_buy_offers` and `nft_sell_offers` RPC commands. Both commands need identical pagination and JSON-serialization behavior — they differ only in which ledger directory they traverse (buy-side vs. sell-side). Rather than duplicate that logic, this file factors it into two `inline` free functions that each handler calls with a different `Keylet`.

## The Two-Function Contract

`appendNftOfferJson` is a pure serializer: given a single `SLE` representing an `ltNFTOKEN_OFFER` ledger object, it appends a JSON object to an accumulator array. It always emits `nft_offer_index` (the offer's ledger key), `flags`, `owner`, and `amount`. The optional fields `destination` and `expiration` are emitted only when present in the SLE, using `isFieldPresent` rather than accessing them unconditionally — this correctly reflects that these fields are optional in the on-ledger format.

`enumerateNFTOffers` is the pagination engine. It accepts the full RPC `context`, the target `nftId`, and a `directory` keylet (either `keylet::nft_buys(nftId)` or `keylet::nft_sells(nftId)`). It resolves the ledger, walks the directory with optional cursor resumption, and returns the complete JSON response.

## Pagination Design

The pagination pattern follows the standard XRPL RPC cursor model but has a subtle off-by-one nuance worth understanding. The `limit` parameter is bounded by `RPC::Tuning::nftOffers` (min 50, default 250, max 500).

**Fresh queries (no `marker`):** `reserve` is set to `limit + 1`. `forEachItemAfter` is called with this inflated reserve. If exactly `reserve` items come back, the extra item signals that more pages exist: the last item is captured as the next `marker`, then popped off before serialization. If fewer than `reserve` items come back, the result fits on a single page and no marker is emitted.

**Resume queries (with `marker`):** The marker string is parsed as a hex `uint256` — the ledger key of the last offer seen in the previous response. The code immediately reads that SLE and validates that its `sfNFTokenID` matches the requested `nftId`. This cross-token check prevents a client from accidentally or maliciously using a marker from one NFT's offer list to paginate through another's. The `sfNFTokenOfferNode` field from that SLE is extracted as `startHint` — this is the directory page number, which `forEachItemAfter` uses to jump directly to the right page rather than scanning from page zero. The marker offer itself is appended immediately (it was the "last seen" item, now it becomes the first item of the new page). `reserve` remains `limit` (not incremented), and `forEachItemAfter` fetches up to `reserve` more items after the marker position.

## Interaction with `forEachItemAfter`

The underlying traversal is provided by `forEachItemAfter` from `xrpl/ledger/helpers/DirectoryHelpers.h`. That function walks a linked-list of directory pages, invoking a callback for each entry. The callback here filters strictly for `ltNFTOKEN_OFFER` type objects — any other object type in the directory causes the callback to return `false` and stops iteration. If `forEachItemAfter` itself returns `false` (indicating a corrupted or inconsistent directory structure), `enumerateNFTOffers` returns `rpcINVALID_PARAMS`, treating the marker as invalid.

## Rate Limiting

After successful enumeration, `context.loadType = Resource::feeMediumBurdenRPC` is set. This is not an error indicator — it is a signal to the RPC framework's resource tracking system that this request carries a medium cost, which influences throttling decisions for clients who issue many such calls.

## Why a Header-Only Design

The `doNFTBuyOffers` and `doNFTSellOffers` implementations in their respective `.cpp` files are trivially thin: parse the `nft_id` parameter, then call `enumerateNFTOffers` with the appropriate directory keylet. The entire substance of both commands lives here. Placing the shared logic in a header with `inline` functions avoids creating a separate `.cpp` translation unit just to share ~100 lines of code, and keeps the relationship between the two handlers immediately visible to anyone reading either handler file.