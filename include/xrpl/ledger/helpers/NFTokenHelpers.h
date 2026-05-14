/**
 * @file NFTokenHelpers.h
 * @brief Core helpers for NFT paged-directory and offer management.
 *
 * Declares all mutable and read-only operations on the NFToken paged-directory
 * structure and offer queues.  Every transaction that touches an NFToken —
 * minting, burning, transferring, or creating/cancelling offers — calls these
 * helpers rather than manipulating ledger state directly.
 *
 * @note NFTs are packed into doubly-linked `ltNFTOKEN_PAGE` SLEs, each
 *     holding up to `kDIR_MAX_TOKENS_PER_PAGE` (32) tokens sorted by
 *     `compareTokens()`.  Tokens sharing the same low-96-bit masked value
 *     (issuer + taxon) are *equivalent* and must be collocated on the same
 *     page.  Page key invariant: every token's low 96 bits are strictly less
 *     than the low 96 bits of its enclosing page key.
 */

#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/nft.h>

#include <utility>

namespace xrpl::nft {

/** Delete up to a specified number of offers from the specified token offer
 *  directory.
 *
 *  Iterates the directory page-by-page, deleting offers in reverse index order
 *  within each page. Reverse iteration is required because `sfIndexes` is
 *  vector-backed and forward deletion would corrupt the remaining indices.
 *  Stops as soon as `maxDeletableOffers` offers have been removed.
 *
 *  @param view The apply view to mutate.
 *  @param directory Keylet of the NFT buy or sell offer directory to drain.
 *  @param maxDeletableOffers Maximum number of offers to remove in this call.
 *  @return The number of offers actually deleted.
 *  @note Returns 0 immediately if `maxDeletableOffers` is 0. Used by
 *      `NFTokenBurn` to drain open offers within the per-transaction
 *      deletion cap (`maxDeletableTokenOfferEntries`).
 */
std::size_t
removeTokenOffersWithLimit(
    ApplyView& view,
    Keylet const& directory,
    std::size_t maxDeletableOffers);

/** Finds the specified token in the owner's token directory.
 *
 *  Read-only traversal: locates the `ltNFTOKEN_PAGE` candidate via `succ()`
 *  and searches the page's `sfNFTokens` array for a matching `sfNFTokenID`.
 *
 *  @param view The read-only view to query.
 *  @param owner The account whose NFT directory is searched.
 *  @param nftokenID The 256-bit NFT identifier to look up.
 *  @return The matching token `STObject`, or `std::nullopt` if not found.
 *  @see findTokenAndPage for the mutable overload that also returns the page.
 */
std::optional<STObject>
findToken(ReadView const& view, AccountID const& owner, uint256 const& nftokenID);

/** Token and its containing page, returned by `findTokenAndPage()`.
 *
 *  Bundles the located token `STObject` with the mutable `shared_ptr<SLE>`
 *  page so callers can modify the token in place without a second ledger
 *  traversal. The page pointer must be used exclusively on the same
 *  `ApplyView` that produced it.
 */
struct TokenAndPage
{
    STObject token;
    std::shared_ptr<SLE> page;

    TokenAndPage(STObject token, std::shared_ptr<SLE> page)
        : token(std::move(token)), page(std::move(page))
    {
    }
};

/** Finds the token in the owner's token directory and returns it with its page.
 *
 *  Mutable traversal via `ApplyView::peek()`. Returns both the token
 *  `STObject` and the `shared_ptr<SLE>` page so that callers such as
 *  `NFTokenAcceptOffer` can pass the page directly to `removeToken()`,
 *  avoiding a redundant page lookup.
 *
 *  @param view The apply view to query (mutable; uses `peek()`).
 *  @param owner The account whose NFT directory is searched.
 *  @param nftokenID The 256-bit NFT identifier to look up.
 *  @return A `TokenAndPage` containing the token and its page, or
 *      `std::nullopt` if the token is not found.
 *  @see findToken for the read-only alternative that returns only the token.
 */
std::optional<TokenAndPage>
findTokenAndPage(ApplyView& view, AccountID const& owner, uint256 const& nftokenID);

/** Insert the token in the owner's token directory.
 *
 *  Locates or creates the appropriate `ltNFTOKEN_PAGE` via `getPageForToken()`.
 *  If the target page is full, it is split to make room; each split increments
 *  the owner's reserve count. Tokens are kept sorted within a page by
 *  `compareTokens()` (low 96-bit key first, full ID as tiebreaker).
 *
 *  @param view The apply view to mutate.
 *  @param owner The account that will own the token.
 *  @param nft The token `STObject` to insert; must contain `sfNFTokenID`.
 *  @return `tesSUCCESS` on success, or `tecNO_SUITABLE_NFTOKEN_PAGE` if the
 *      target page is entirely filled with equivalent tokens (same low 96-bit
 *      key) and no split is possible.
 */
TER
insertToken(ApplyView& view, AccountID owner, STObject&& nft);

/** Remove the token from the owner's token directory.
 *
 *  Page-discovery overload: locates the containing `ltNFTOKEN_PAGE` via
 *  `succ()` and then delegates to the two-argument form.  Use this when
 *  the caller does not already hold a page reference.
 *
 *  After erasure, attempts to merge the affected page with its neighbours;
 *  each successful merge credits one reserve. If the page becomes empty it
 *  is unlinked and erased.
 *
 *  @param view The apply view to mutate.
 *  @param owner The account that currently holds the token.
 *  @param nftokenID The 256-bit NFT identifier to remove.
 *  @return `tesSUCCESS`, or `tecNO_ENTRY` if the page or token cannot be
 *      found.
 *  @see removeToken(ApplyView&, AccountID const&, uint256 const&, shared_ptr<SLE> const&)
 *      for the overload that skips the page lookup.
 */
TER
removeToken(ApplyView& view, AccountID const& owner, uint256 const& nftokenID);

/** Remove the token from the owner's token directory using a pre-located page.
 *
 *  Caller-supplied page overload: skips the `succ()`-based page lookup when
 *  the caller already holds the page (e.g., from `findTokenAndPage()`).
 *  The `page` pointer must have been obtained from the same `ApplyView`
 *  instance.
 *
 *  Under the `fixNFTokenPageLinks` amendment, if the emptied page is the final
 *  anchor page (`nftpage_max`), its contents are replaced with those of the
 *  previous page and the now-empty previous page is erased, preserving the
 *  invariant that the last page always has the stable sentinel key.
 *
 *  @param view The apply view to mutate.
 *  @param owner The account that currently holds the token.
 *  @param nftokenID The 256-bit NFT identifier to remove.
 *  @param page The mutable SLE page known to contain the token.
 *  @return `tesSUCCESS`, or `tecNO_ENTRY` if the token is not found on the
 *      supplied page.
 */
TER
removeToken(
    ApplyView& view,
    AccountID const& owner,
    uint256 const& nftokenID,
    std::shared_ptr<SLE> const& page);

/** Deletes the given token offer and removes it from both tracking directories.
 *
 *  An offer is tracked in two separate places:
 *      - The token's `nft_buys` directory, if it is a buy offer; or
 *      - The token's `nft_sells` directory, if it is a sell offer; and
 *      - The owner's owner directory.
 *
 *  Both directory entries are removed, the owner's reserve count is
 *  decremented by one, and the offer SLE is erased.
 *
 *  @param view The apply view to mutate.
 *  @param offer The SLE for the offer to delete; must be of type
 *      `ltNFTOKEN_OFFER`.
 *  @return `true` if the offer was successfully deleted; `false` if the SLE
 *      is not of type `ltNFTOKEN_OFFER` or if a directory removal fails,
 *      acting as a type-safety guard.
 */
bool
deleteTokenOffer(ApplyView& view, std::shared_ptr<SLE> const& offer);

/** Repairs the links in an NFToken page directory.
 *
 *  Walks the entire `ltNFTOKEN_PAGE` chain for the owner and corrects any
 *  broken `sfNextPageMin` / `sfPreviousPageMin` links. If the final page does
 *  not have the expected `nftpage_max` sentinel key, its contents are migrated
 *  to a newly created SLE with the correct key, the old SLE is erased, and the
 *  chain is relinked. Owner count is unchanged by this operation because the
 *  page count is preserved.
 *
 *  Intended to be called by the `LedgerStateFix` transaction on accounts with
 *  known directory corruption.
 *
 *  @param view The apply view to mutate.
 *  @param owner The account whose NFToken page directory is to be repaired.
 *  @return `true` if any correction was applied; `false` if the directory was
 *      already consistent.
 */
bool
repairNFTokenDirectoryLinks(ApplyView& view, AccountID const& owner);

/** Ordering predicate for NFToken IDs within and across pages.
 *
 *  Sorts first by the low 96 bits of each ID (the `pageMask` region that
 *  determines page placement), then by the full 256-bit value as a
 *  tiebreaker. This ensures deterministic ordering for tokens that share
 *  the same low 96-bit prefix (equivalent tokens) and must co-reside on
 *  a single page.
 *
 *  @param a First NFToken ID.
 *  @param b Second NFToken ID.
 *  @return `true` if `a` sorts before `b`.
 */
bool
compareTokens(uint256 const& a, uint256 const& b);

/** Modify the URI of an existing NFToken in the owner's directory.
 *
 *  Locates the token's page and updates the `sfURI` field in the token's
 *  `STObject` within the page's `sfNFTokens` array. If `uri` is
 *  `std::nullopt`, the `sfURI` field is removed from the token.
 *
 *  @param view The apply view to mutate.
 *  @param owner The account that owns the token.
 *  @param nftokenID The 256-bit NFT identifier whose URI is to be changed.
 *  @param uri The new URI value, or `std::nullopt` to clear the URI.
 *  @return `tesSUCCESS` on success, or `tecINTERNAL` if the page or token
 *      cannot be located (indicates ledger inconsistency).
 */
TER
changeTokenURI(
    ApplyView& view,
    AccountID const& owner,
    uint256 const& nftokenID,
    std::optional<xrpl::Slice> const& uri);

/** Preflight checks shared by NFTokenCreateOffer and NFTokenMint.
 *
 *  Validates offer parameters that require no ledger access: negative or
 *  zero amounts (buy offers must carry a non-zero amount), zero IOU amounts,
 *  zero expiration, and malformed `owner`/`destination` combinations.
 *  A buy offer must supply `owner` (the targeted token holder); a sell offer
 *  must not (the seller is implicit). Neither party may designate itself as
 *  the destination.
 *
 *  Defaults (`owner = nullopt`, `txFlags = tfSellNFToken`) allow
 *  `NFTokenMint` to reuse this path with minimal adaptation.
 *
 *  @param acctID Account executing the transaction.
 *  @param amount The offer amount; must be non-negative and, for buy offers,
 *      non-zero and non-zero for IOUs.
 *  @param dest Optional destination account that may exclusively accept the
 *      offer; must not equal `acctID`.
 *  @param expiration Optional offer expiration; must not be zero.
 *  @param nftFlags The flags field of the NFToken being offered.
 *  @param rules Current ledger rule set used for amendment checks.
 *  @param owner For buy offers, the account that currently holds the token;
 *      must be absent for sell offers.
 *  @param txFlags Transaction flags; `tfSellNFToken` distinguishes sell from
 *      buy.
 *  @return `tesSUCCESS` if all static checks pass, or a `temXXX` error code
 *      indicating which parameter is invalid.
 */
NotTEC
tokenOfferCreatePreflight(
    AccountID const& acctID,
    STAmount const& amount,
    std::optional<AccountID> const& dest,
    std::optional<std::uint32_t> const& expiration,
    std::uint16_t nftFlags,
    Rules const& rules,
    std::optional<AccountID> const& owner = std::nullopt,
    std::uint32_t txFlags = tfSellNFToken);

/** Preclaim checks shared by NFTokenCreateOffer and NFTokenMint.
 *
 *  Accesses the ledger to validate conditions that cannot be checked
 *  statically:
 *  - For non-XRP offers on tokens without `flagCreateTrustLines`, verifies
 *    that the NFT issuer's trust line for the IOU exists and is not frozen.
 *    Under `featureNFTokenMintOffer`, an issuer selling their own currency is
 *    exempt from this check.
 *  - Enforces `flagTransferable`: if absent and the transacting account is
 *    neither the issuer nor the current `sfNFTokenMinter`, returns
 *    `tefNFTOKEN_IS_NOT_TRANSFERABLE`.
 *  - For buy offers, verifies the account currently has sufficient funds.
 *  - Verifies `dest` and `owner` accounts exist and have not set
 *    `lsfDisallowIncomingNFTokenOffer`.
 *  - Under `fixEnforceNFTokenTrustlineV2`, calls `checkTrustlineAuthorized()`
 *    to reject offers backed by unauthorized trust lines that carry a balance.
 *
 *  @param view The read-only ledger view.
 *  @param acctID Account executing the transaction.
 *  @param nftIssuer Issuer encoded in the NFToken ID.
 *  @param amount The offer amount.
 *  @param dest Optional restricted destination account.
 *  @param nftFlags The flags field of the NFToken being offered.
 *  @param xferFee Transfer fee encoded in the NFToken ID (basis points).
 *  @param j Journal for diagnostic logging.
 *  @param owner For buy offers, the account that currently holds the token.
 *  @param txFlags Transaction flags; `tfSellNFToken` distinguishes sell from
 *      buy.
 *  @return `tesSUCCESS` if all ledger-state checks pass, or a `tecXXX` /
 *      `tefXXX` error code.
 */
TER
tokenOfferCreatePreclaim(
    ReadView const& view,
    AccountID const& acctID,
    AccountID const& nftIssuer,
    STAmount const& amount,
    std::optional<AccountID> const& dest,
    std::uint16_t nftFlags,
    std::uint16_t xferFee,
    beast::Journal j,
    std::optional<AccountID> const& owner = std::nullopt,
    std::uint32_t txFlags = tfSellNFToken);

/** doApply implementation shared by NFTokenCreateOffer and NFTokenMint.
 *
 *  Reserves XRP for the new `ltNFTOKEN_OFFER` object, inserts the offer into
 *  the account's owner directory and into the token's buy or sell directory
 *  (determined by `tfSellNFToken` in `txFlags`), constructs the SLE with the
 *  supplied fields, and increments the owner count.
 *
 *  @param view The apply view to mutate.
 *  @param acctID Account executing the transaction.
 *  @param amount The offer amount.
 *  @param dest Optional restricted destination account.
 *  @param expiration Optional expiration time for the offer.
 *  @param seqProxy Sequence or ticket proxy used to derive the offer keylet.
 *  @param nftokenID The 256-bit ID of the NFToken being offered.
 *  @param priorBalance The account's XRP balance before the transaction fee
 *      was deducted; used to verify the reserve requirement.
 *  @param j Journal for diagnostic logging.
 *  @param txFlags Transaction flags; `tfSellNFToken` controls offer direction.
 *  @return `tesSUCCESS` on success, `tecINSUFFICIENT_RESERVE` if the account
 *      cannot cover the new object reserve, or `tecDIR_FULL` if either
 *      directory is at capacity.
 */
TER
tokenOfferCreateApply(
    ApplyView& view,
    AccountID const& acctID,
    STAmount const& amount,
    std::optional<AccountID> const& dest,
    std::optional<std::uint32_t> const& expiration,
    SeqProxy seqProxy,
    uint256 const& nftokenID,
    XRPAmount const& priorBalance,
    beast::Journal j,
    std::uint32_t txFlags = tfSellNFToken);

/** Verify that an account is authorized to hold a given IOU trust line.
 *
 *  Only active under the `fixEnforceNFTokenTrustlineV2` amendment; returns
 *  `tesSUCCESS` unconditionally when the amendment is not enabled.
 *
 *  When active, checks that if the IOU issuer requires authorization
 *  (`lsfRequireAuth`), the trust line between `id` and the issuer exists and
 *  carries the appropriate `lsfLowAuth` / `lsfHighAuth` flag. The issuer
 *  account is always considered authorized to hold its own issuance.
 *
 *  @param view The read-only ledger view.
 *  @param id The account whose authorization is being verified.
 *  @param j Journal for diagnostic logging.
 *  @param issue The IOU issue (currency + issuer) to check; must not be XRP.
 *  @return `tesSUCCESS` if authorized, `tecNO_ISSUER` if the issuer account
 *      does not exist, `tecNO_LINE` if the required trust line is absent, or
 *      `tecNO_AUTH` if the trust line exists but is not authorized.
 *  @note Only valid for custom (non-XRP) currencies; asserts otherwise.
 */
TER
checkTrustlineAuthorized(
    ReadView const& view,
    AccountID const id,
    beast::Journal const j,
    Issue const& issue);

/** Verify that an IOU trust line is not deep-frozen for a given account.
 *
 *  Only active under the `featureDeepFreeze` amendment; returns
 *  `tesSUCCESS` unconditionally when the amendment is not enabled.
 *
 *  When active, checks whether the trust line between `id` and the IOU issuer
 *  carries either `lsfLowDeepFreeze` or `lsfHighDeepFreeze`. Either side
 *  enacting deep freeze blocks token receipt, regardless of which party set it.
 *  The issuer account is always permitted to accept its own issuance; accounts
 *  with no trust line are treated as not frozen.
 *
 *  @param view The read-only ledger view.
 *  @param id The account whose deep-freeze status is being checked.
 *  @param j Journal for diagnostic logging.
 *  @param issue The IOU issue (currency + issuer) to check; must not be XRP.
 *  @return `tesSUCCESS` if not deep-frozen or if no trust line exists,
 *      `tecNO_ISSUER` if the issuer account does not exist, or `tecFROZEN`
 *      if the trust line is deep-frozen.
 *  @note Only valid for custom (non-XRP) currencies; asserts otherwise.
 */
TER
checkTrustlineDeepFrozen(
    ReadView const& view,
    AccountID const id,
    beast::Journal const j,
    Issue const& issue);

}  // namespace xrpl::nft
