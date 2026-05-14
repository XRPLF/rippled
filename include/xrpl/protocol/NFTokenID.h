/** @file
 *  Helpers that reconstruct NFToken identities from transaction metadata
 *  and inject them into RPC JSON responses as synthetic fields.
 *
 *  Raw ledger metadata records before/after state of `NFTokenPage` objects
 *  but does not directly annotate which token was created or consumed. The
 *  functions below bridge that gap. They are free (non-static) functions so
 *  that Clio (the XRPL History API server) can link against them directly
 *  and perform the same enrichment without duplicating the logic.
 *
 *  @see NFTokenOfferID.h for the analogous helpers for `NFTokenOffer` IDs.
 */

#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TxMeta.h>

#include <memory>
#include <optional>
#include <vector>

namespace xrpl {

/** Returns true if this transaction could have produced or consumed an NFToken.
 *
 *  Acts as a cheap early-exit guard for all downstream extraction logic.
 *  A transaction qualifies only when it is one of the three NFT types
 *  (`ttNFTOKEN_MINT`, `ttNFTOKEN_ACCEPT_OFFER`, `ttNFTOKEN_CANCEL_OFFER`)
 *  and its result code is `tesSUCCESS`. A failed transaction cannot have
 *  mutated any NFToken page, so metadata diffing would be meaningless.
 *
 *  @param serializedTx     The executed transaction; a null pointer yields
 *      false immediately.
 *  @param transactionMeta  Metadata from the same transaction, used to
 *      check the result code.
 *  @return True only when `serializedTx` is non-null, its type is one of
 *      the three NFT transaction types, and the result is `tesSUCCESS`.
 */
bool
canHaveNFTokenID(std::shared_ptr<STTx const> const& serializedTx, TxMeta const& transactionMeta);

/** Recovers the ID of the NFToken added by a mint transaction.
 *
 *  `ttNFTOKEN_MINT` metadata records the full token arrays of every
 *  affected `NFTokenPage` in `sfPreviousFields` and `sfFinalFields` but
 *  does not tag the newly inserted entry. This function recovers it by
 *  set-difference: it accumulates token IDs from all previous states into
 *  `prevIDs` and all final states into `finalIDs`, then uses
 *  `std::mismatch` to locate the first entry present in `finalIDs` but
 *  absent from `prevIDs`. Because `NFTokenPage` entries are stored in
 *  sorted order by token ID, both vectors are already ordered and
 *  `std::mismatch` finds the insertion point in linear time without
 *  additional sorting.
 *
 *  @note When a mint causes an existing page to split, the linked-list
 *      rewiring may produce a `sfModifiedNode` for a third page whose
 *      `sfPreviousFields` contain only pointer updates (`NextPageMin` /
 *      `PreviousPageMin`) with no `sfNFTokens` array. Such nodes are
 *      skipped silently; without this guard the size invariant below
 *      would incorrectly fail for legitimate mints.
 *
 *  @param transactionMeta  Metadata from a `ttNFTOKEN_MINT` transaction.
 *  @return The `uint256` ID of the newly minted token, or `std::nullopt`
 *      if `finalIDs.size() != prevIDs.size() + 1` (tokens are minted one
 *      at a time) or if `std::mismatch` unexpectedly reaches the end of
 *      `finalIDs`.
 */
std::optional<uint256>
getNFTokenIDFromPage(TxMeta const& transactionMeta);

/** Collects the NFToken IDs referenced by deleted `NFTokenOffer` objects.
 *
 *  Both `ttNFTOKEN_ACCEPT_OFFER` and `ttNFTOKEN_CANCEL_OFFER` delete one
 *  or more `ltNFTOKEN_OFFER` ledger entries. Each deleted offer's
 *  `sfFinalFields` carries the `sfNFTokenID` it was created for, so the
 *  token identity is recoverable without set-difference arithmetic.
 *  Results are sorted and deduplicated because a single cancel transaction
 *  can target multiple offers that reference the same underlying NFT.
 *
 *  @param transactionMeta  Metadata from a `ttNFTOKEN_ACCEPT_OFFER` or
 *      `ttNFTOKEN_CANCEL_OFFER` transaction.
 *  @return Sorted, deduplicated vector of `uint256` NFToken IDs recovered
 *      from all deleted offer nodes; empty if no qualifying deletions are
 *      found.
 */
std::vector<uint256>
getNFTokenIDFromDeletedOffer(TxMeta const& transactionMeta);

/** Injects synthetic NFToken ID field(s) into an RPC transaction response.
 *
 *  Calls `canHaveNFTokenID` first; returns immediately without modifying
 *  `response` if the transaction is ineligible or extraction yields nothing.
 *  When eligible, dispatches by transaction type:
 *
 *  - `ttNFTOKEN_MINT` — writes `jss::nftoken_id` (single string) derived
 *    from `getNFTokenIDFromPage`.
 *  - `ttNFTOKEN_ACCEPT_OFFER` — writes `jss::nftoken_id` (single string,
 *    first element) derived from `getNFTokenIDFromDeletedOffer`.
 *  - `ttNFTOKEN_CANCEL_OFFER` — writes `jss::nftoken_ids` (JSON array of
 *    all deduplicated IDs) derived from `getNFTokenIDFromDeletedOffer`.
 *
 *  The singular/plural field-name distinction reflects a real semantic
 *  difference: accept and mint affect exactly one NFT, while cancel can
 *  affect many.
 *
 *  @param response         The JSON object to enrich; fields are written
 *      directly into it. The caller is responsible for scoping this to
 *      the `jss::meta` sub-object of the full response.
 *  @param transaction      The executed transaction. A null pointer is
 *      handled gracefully via `canHaveNFTokenID`.
 *  @param transactionMeta  Read-only metadata used for eligibility checking
 *      and token ID extraction.
 */
void
insertNFTokenID(
    json::Value& response,
    std::shared_ptr<STTx const> const& transaction,
    TxMeta const& transactionMeta);

}  // namespace xrpl
