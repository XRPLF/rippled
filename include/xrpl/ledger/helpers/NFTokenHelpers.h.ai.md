# `include/xrpl/ledger/helpers/NFTokenHelpers.h`

This header is the central API surface for NFT lifecycle management within the XRPL ledger. It lives in the `xrpl::nft` namespace and declares every function needed to insert, remove, and query NFTs in the on-ledger token directory, manage buy/sell offers, and execute the three transaction-processing phases (preflight, preclaim, doApply) for offer creation. The implementations live in `src/libxrpl/ledger/helpers/NFTokenHelpers.cpp`, which is the most complex helper module in the ledger subsystem.

## NFToken Storage Architecture

NFTs are not stored as individual ledger objects. Instead, an account's entire NFT holdings are packed into a doubly-linked list of `ltNFTOKEN_PAGE` SLEs (serialized ledger entries), each holding up to `dirMaxTokensPerPage` tokens as an `STArray`. Pages are keyed in the ledger by a `Keylet` derived from the account ID and the low 96-bits of the NFT ID (the `pageMask`). The chain is anchored by a deterministic "max" page at `keylet::nftpage_max(owner)`, which always exists as the final node.

Within a page, tokens are sorted by `compareTokens()`, which orders first by the low 96-bits of the token ID and then by the full 256-bit value as a tiebreaker. This two-level sort exists because the page partitioning scheme uses only the low 96-bits for page boundaries: tokens with identical low 96-bits are "equivalent" and must all reside on the same page. The full-value fallback ensures a stable, deterministic ordering for those groups.

## Token Directory Operations

`insertToken()` delegates the hard work to the file-private `getPageForToken()`. If the correct page is full, the page is split. The split algorithm uses the `pageMask` to identify equivalent-token groups and never bisects such a group — it rounds the split point forward or backward as needed. If an entire page is filled with equivalent tokens and a new token would belong to the same group, `getPageForToken()` returns `nullptr`, causing `insertToken()` to return `tecNO_SUITABLE_NFTOKEN_PAGE`. Each page split increments the owner's reserve count via `adjustOwnerCount()`.

`removeToken()` performs the inverse: it erases the token from the containing page, then attempts to merge the page with its neighbours using the file-private `mergePages()`. Merging fires a reserve credit. If the page becomes empty, it is unlinked and erased entirely. A special case applies under the `fixNFTokenPageLinks` amendment: if the empty page happens to be the last page in the directory (identifiable because its key matches `pageMask`), its contents are moved to the previous page and the empty last page is kept with the stable anchor key, preserving the invariant that the final page is always the `nftpage_max` sentinel.

`findToken()` takes a `ReadView` (read-only) and returns an `std::optional<STObject>`. `findTokenAndPage()` requires an `ApplyView` (read-write) and returns the `TokenAndPage` aggregate, which bundles the token `STObject` with the mutable `shared_ptr<SLE>` page. Returning the page is a deliberate optimization: callers like `NFTokenModify` that need to alter the token in place can do so without a second ledger traversal to re-locate the page.

## Offer Lifecycle

Each NFT offer is tracked in exactly two directories: the owner's owner directory and either the token's buy or sell directory (`keylet::nft_buys` / `keylet::nft_sells`). `deleteTokenOffer()` removes the offer from both directories, decrements the owner count, and erases the SLE. It returns `false` if the SLE is the wrong type, acting as a type-safety guard.

`removeTokenOffersWithLimit()` is the bulk-deletion primitive used when burning a token. It iterates the given offer directory page by page in reverse order within each page, calling `deleteTokenOffer()` until the `maxDeletableOffers` cap is reached. Reverse iteration within a page is necessary because NFTokenOffer directory pages use a vector-backed `sfIndexes` field and forward-erasing would corrupt iterators.

`notTooManyOffers()` is a burn guard run during preclaim. It counts all open offers across both buy and sell directories (iterating page-by-page for efficiency) and returns `tefTOO_BIG` if the total exceeds `maxDeletableTokenOfferEntries`. This prevents a token with a pathologically large offer set from being impossible to burn.

## Shared Transaction Phases

`tokenOfferCreatePreflight`, `tokenOfferCreatePreclaim`, and `tokenOfferCreateApply` are shared between `NFTokenCreateOffer` and `NFTokenMint` (which can implicitly create a sell offer at mint time). Centralising this logic avoids duplicating the involved validation rules.

`tokenOfferCreatePreflight` performs static checks requiring no ledger access: negative amounts, zero amounts for buy offers, zero-value IOU amounts, zero expiration, and malformed `Owner`/`Destination` combinations. A buy offer must name an `owner` (the token holder being targeted); a sell offer must not, because the seller is implicit. Neither side may designate itself as the destination.

`tokenOfferCreatePreclaim` accesses the ledger. For non-XRP offers on tokens without `flagCreateTrustLines`, it verifies the NFT issuer's trust line exists for the IOU and is not frozen. Under the `featureNFTokenMintOffer` amendment, an issuer selling their own currency is exempted from the trust line check. Transferability is enforced: if `flagTransferable` is absent and the transacting account is neither the issuer nor the current `sfNFTokenMinter`, the preclaim returns `tefNFTOKEN_IS_NOT_TRANSFERABLE`. The `fixEnforceNFTokenTrustlineV2` amendment adds a call to `checkTrustlineAuthorized()`, closing a loophole where unauthorized trust lines with a positive balance could be used for buy offers.

`tokenOfferCreateApply` reserves XRP for the new offer object, inserts the offer into both directories (owner directory and token offer directory), constructs the `ltNFTOKEN_OFFER` SLE, and increments the owner count.

## Repair Utility

`repairNFTokenDirectoryLinks()` is a ledger repair tool invoked by the `LedgerStateFix` transaction. It walks the entire NFToken page chain for an account and corrects any broken `sfNextPageMin` / `sfPreviousPageMin` links. It also handles the case where the last page does not have the expected `nftpage_max` key — in that scenario the page contents are migrated to a newly created SLE with the correct key, the old SLE is erased, and the chain is relinked. The function returns `true` if any correction was made, giving the caller a way to avoid unnecessary ledger updates on clean accounts.

## Design Notes

The two overloads of `removeToken()` reflect a deliberate API split: the single-argument overload performs page discovery internally (convenient for callers that don't already hold a page reference), while the overload that accepts a `shared_ptr<SLE> page` is for callers like `NFTokenBurn` that already located the page through `findTokenAndPage()` and want to avoid repeating the traversal. The `tokenOfferCreate*` triad using `owner = std::nullopt` and `txFlags = tfSellNFToken` as defaults lets `NFTokenMint` pass through the same validation pipeline as `NFTokenCreateOffer` with minimal parameter adaptation.