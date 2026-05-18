# `NFTSellOffers.cpp` — RPC Handler for `nft_sell_offers`

This file implements `doNFTSellOffers`, the entry-point handler for the `nft_sell_offers` RPC method. Its job is minimal by design: validate the caller-supplied NFT identifier and route the request to the shared enumeration logic in `NFTOffersHelpers.h`.

## Role in the System

The `nft_sell_offers` method allows clients to query all active sell offers associated with a specific NFT on the XRP Ledger. The handler lives alongside its mirror image `NFTBuyOffers.cpp` in the `orderbook` handler group — the two files are structurally identical, differing only in which on-ledger directory keylet they pass to the shared helper.

## Handler Logic

`doNFTSellOffers` performs two sequential input validations before doing anything else. First it checks that `nft_id` is present in the request parameters, returning a `missing_field_error` immediately if not. Then it attempts to parse the string value as a 256-bit hex identifier via `uint256::parseHex`; a failure here produces an `invalid_field_error`. These two guards mean any downstream code can rely on `nftId` being a well-formed `uint256`.

The actual work is entirely delegated to `enumerateNFTOffers` (defined inline in `NFTOffersHelpers.h`), which handles ledger selection, pagination via markers, directory traversal using `forEachItemAfter`, and JSON serialization of each offer through `appendNftOfferJson`. The critical argument distinguishing this handler from `doNFTBuyOffers` is the keylet: `keylet::nft_sells(nftId)` targets the NFT's sell-offer directory on the ledger, whereas the buy-offer handler passes `keylet::nft_buys(nftId)` instead. Both directories are maintained by the ledger as linked lists of `ltNFTOKEN_OFFER` objects.

## Design Choice: Thin Handler, Shared Core

Keeping the handler file at roughly 12 lines of functional code is a deliberate separation of concerns. Input validation and routing belong here; result construction, ledger access, and pagination logic belong in the shared helper. This avoids duplicating the pagination fence-post arithmetic and marker validation between the buy and sell variants, both of which would otherwise be identical.