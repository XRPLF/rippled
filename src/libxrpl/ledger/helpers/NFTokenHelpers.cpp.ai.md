# `NFTokenHelpers.cpp` — NFT Directory and Offer Management

## Purpose and Place in the System

This file is the core implementation layer for all NFT data-structure operations in the XRP Ledger. Every transaction that touches an NFToken — minting, burning, transferring, or creating/cancelling offers — ultimately calls into these helpers rather than manipulating ledger state directly. The file lives in `libxrpl/ledger/helpers/` alongside analogous helpers for regular directories and RippleState objects, a separation that keeps the NFT-specific page structure logic out of individual transactor files.

## The NFT Page Data Structure

NFTs are not stored as individual ledger objects. An account's entire NFT portfolio is packed into a doubly-linked list of `ltNFTOKEN_PAGE` SLEs, each holding up to `dirMaxTokensPerPage` (32) tokens as an `STArray`. The chain is anchored by a deterministic "max" page whose key is derived from `keylet::nftpage_max(owner)`. This page always acts as the tail; all real pages have keys less than it.

Pages are keyed using a combination of the owner's `AccountID` and the low 96 bits of an NFToken ID, which are exposed through `nft::pageMask`. These low 96 bits encode the issuer and taxon — tokens that share the same masked value are considered **equivalent** and must be collocated on the same page. The sort comparator `compareTokens()` reflects this: it sorts on the low 96 bits first, then on the full ID as a tiebreaker, creating a stable total order.

The page key invariant is that the low 96 bits of every NFToken stored in a page must be **strictly less than** the low 96 bits of the page's own key. This is why page keys are chosen one higher than the largest token they contain (using `uint256::next()`).

## Page Location: `locatePage`

The file provides two overloads — one taking `ReadView const&` (returning `shared_ptr<SLE const>`) and one taking `ApplyView&` (returning mutable `shared_ptr<SLE>`). Both use the same strategy: compute the theoretical minimum page key for the token using `keylet::nftpage(keylet::nftpage_min(owner), id)`, then call `view.succ()` to find the first *actual* page key that is strictly greater than it and within the owner's range. If no such key exists, `succ()` returns `nullopt` and the code falls back to the max-page key, which will either be an existing page or produce a `nullptr` from `view.read/peek`.

The choice to use `view.succ()` rather than scanning the linked list is critical for performance: it leverages the ledger's sorted B-tree structure to find the candidate page in O(log N) time rather than walking every page in the chain.

## Page Creation and Splitting: `getPageForToken`

`getPageForToken` is the mutable counterpart that also creates pages on demand. When no suitable page exists, it creates a new SLE at `keylet::nftpage_max(owner)` and invokes a `createCallback` (used by callers to increment the owner reserve count). When a suitable page is found but is already full, it must split.

The splitting algorithm is the most complex logic in the file. Rather than splitting exactly at the midpoint, it must find a split point at an equivalent-group boundary. It starts at the midpoint and advances forward past any run of equivalent tokens. If the entire back half is equivalent, it searches from the front of the page instead. Two edge cases require special handling:

- If `splitIter == narr.end()` after both searches, the page is entirely one equivalence class and the token cannot be placed — the function returns `nullptr`.
- If `splitIter == narr.begin()`, the entire page is one class, but the incoming token is a *different* class. The split is decided by comparing the new token's masked value to the page's: if the new token sorts higher, an empty `carr` is created and the new token will go into it; if lower, all content moves to `carr` and the new token occupies the otherwise-empty `narr` side.

After splitting, the new page's key is set to `narr.back().next()` if `narr` is still full, or to `carr.front()` otherwise — preserving the invariant. The doubly-linked list pointers (`sfPreviousPageMin`, `sfNextPageMin`) are updated on up to three pages (new, existing, and predecessor), and the `createCallback` fires once to account for the extra reserved page.

## Token Insertion and Removal

`insertToken()` calls `getPageForToken()` and then inserts the `STObject` into the target page's array, keeping the array sorted via `compareTokens`. It returns `tecNO_SUITABLE_NFTOKEN_PAGE` if the page lookup returned `nullptr` (the equivalence-group-full edge case).

`removeToken()` has two overloads — one discovers the page via `locatePage`, the other accepts a known page (used by `NFTokenBurn` which has already loaded the token). After erasing the entry from the array:

- If the page is **non-empty**, it is updated, then the code attempts to merge it with both its predecessor and successor. This can reduce three pages to one; the owner count is decremented accordingly.
- If the page is **empty** and has a predecessor: under the `fixNFTokenPageLinks` amendment, and if the current page is the chain's tail (key ends with `pageMask`), the predecessor's content is moved into the tail page and the predecessor is erased. This preserves the invariant that the last page always sits at `keylet::nftpage_max`. Without the amendment, the empty page is simply unlinked and erased normally.

The `mergePages()` helper enforces invariants by throwing `std::runtime_error` for out-of-order or link-broken pages before merging — these represent corrupted ledger state and should never occur in practice.

## Offer Management

`insertToken`/`removeToken` cover the NFT itself, but each NFToken can also have buy and sell offer queues. `deleteTokenOffer()` atomically removes a single offer from both the owner's owner-directory and the token's buy/sell directory, then decrements the owner count. `removeTokenOffersWithLimit()` iterates a directory page-by-page in reverse (to avoid invalidating iterators) and calls `deleteTokenOffer` up to a caller-supplied limit. It reads the next-page index before modifying the current page, which is necessary because a fully drained page is itself deleted.

`notTooManyOffers()` is a burn guard: it counts all open offers across both buy and sell directories and returns `tefTOO_BIG` if the total exceeds `maxDeletableTokenOfferEntries` (500). This prevents a token with a pathologically large offer book from becoming impossible to burn.

## Offer Creation: Shared Transaction Logic

The three `tokenOfferCreate*` functions are shared between `NFTokenCreateOffer` and `NFTokenMint` (the latter supports an inline sell-offer at mint time). `tokenOfferCreatePreflight` performs stateless structural validation — negative amounts, zero IOU amounts, sell-offer-without-an-owner, destination equals account. `tokenOfferCreatePreclaim` handles stateful checks against the ledger: NFT issuer trust-line presence for royalty collection, transferability flags (with a minter-exception for the `sfNFTokenMinter` field), IOU frozen checks, destination/owner account existence and `lsfDisallowIncomingNFTokenOffer` flags, and (under `fixEnforceNFTokenTrustlineV2`) trust-line authorization. `tokenOfferCreateApply` inserts the offer SLE into both the owner directory and the token's buy/sell directory, then increments the owner count.

## Directory Repair: `repairNFTokenDirectoryLinks`

`repairNFTokenDirectoryLinks` is a defensive repair function introduced alongside the `fixNFTokenPageLinks` amendment to fix corrupted link fields. It walks the entire page chain for an account by repeatedly calling `view.succ()` from the current page key, comparing expected vs actual `sfNextPageMin`/`sfPreviousPageMin` values. If the discovered last page does not equal `keylet::nftpage_max(owner)` (which can happen due to the historical bug), it creates a new page at the correct key, copies all token data and backlinks into it, erases the imposter page, and returns `true`. The repair never changes the owner count since the page count is unchanged.

## Trust-line Guards

`checkTrustlineAuthorized` and `checkTrustlineDeepFrozen` are IOU-side guards for non-XRP offer amounts. Both are no-ops for XRP (asserted). `checkTrustlineAuthorized` checks that if the currency issuer has `lsfRequireAuth` set, the buyer's trust-line carries the appropriate `lsfLowAuth`/`lsfHighAuth` bit. `checkTrustlineDeepFrozen` checks for the `lsfLowDeepFreeze`/`lsfHighDeepFreeze` bits regardless of which side applied them. Issuers are always exempt from both checks since they cannot hold a trust-line to themselves.