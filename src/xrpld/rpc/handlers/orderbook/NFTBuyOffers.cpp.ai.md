# `NFTBuyOffers.cpp` — RPC Handler for NFT Buy Offer Enumeration

## Role in the System

`NFTBuyOffers.cpp` implements `doNFTBuyOffers`, the server-side handler for the `nft_buy_offers` JSON-RPC method. This method allows clients to retrieve the list of outstanding buy offers for a given NFT on the XRP Ledger. The file is deliberately thin: it owns only input validation and keylet selection, delegating all ledger traversal and response construction to the shared `enumerateNFTOffers` helper.

## Handler Structure

`doNFTBuyOffers` follows the standard two-step validation pattern used throughout the RPC handler layer.

First, it checks for the presence of `nft_id` in the incoming JSON parameters using `context.params.isMember(jss::nft_id)`. If the field is missing, the handler returns immediately via `RPC::missing_field_error`, which emits a well-formed JSON error response without touching the ledger. Second, it attempts to parse the string value as a 256-bit hex identifier using `nftId.parseHex(...)`. A malformed hex string — wrong length, non-hex characters — causes `parseHex` to return false, and the handler responds with `RPC::invalid_field_error`. These two guards ensure that the downstream `enumerateNFTOffers` call always receives a structurally valid `uint256`.

The actual work then dispatches to `enumerateNFTOffers(context, nftId, keylet::nft_buys(nftId))`. The third argument is the architectural differentiator: `keylet::nft_buys(nftId)` produces a `Keylet` that uniquely identifies the on-ledger directory object holding buy-side NFT offers for this token. The corresponding sell-side handler (`NFTSellOffers.cpp`) is structurally identical, differing only in passing `keylet::nft_sells(nftId)` instead.

## Relationship to `NFTOffersHelpers.h`

The real complexity lives in `enumerateNFTOffers` inside `NFTOffersHelpers.h`. That function handles ledger selection (via `RPC::lookupLedger`), existence checks on the offer directory, pagination via `marker`/`limit` parameters, and directory traversal using `forEachItemAfter`. It also serializes each `ltNFTOKEN_OFFER` ledger object into JSON — including fields like `nft_offer_index`, `flags`, `owner`, optional `destination`, optional `expiration`, and `amount` — via `appendNftOfferJson`. The pagination design appends one extra offer beyond the requested limit; if the result set is full, the last entry becomes the `marker` for the next query and is popped from the response, ensuring callers can resume exactly where they left off.

By sharing `enumerateNFTOffers` between the buy and sell handlers, the codebase avoids duplicating pagination logic, ledger access patterns, and error handling. The only caller-supplied distinction is the keylet — the directory structure on the ledger physically separates buy and sell offers per NFT, and the keylet encodes which side to walk.

## Design Notes

The handler does not gate on any feature flags or ledger version checks at this layer; that concern belongs to the registration layer where `doNFTBuyOffers` is wired into the RPC dispatch table. The `context.loadType` is set to `Resource::feeMediumBurdenRPC` inside `enumerateNFTOffers`, reflecting that paginated ledger scans carry non-trivial I/O cost and should be rate-limited accordingly.

The use of `jss::nft_id` — a compile-time string constant from the `jss` namespace — ensures the field name is consistent across all RPC handler code and cannot silently diverge from the wire protocol due to a typo.