/** @file
 *  Reconstructs NFToken identities from transaction metadata and injects
 *  them into RPC JSON responses as synthetic fields.
 *
 *  Raw transaction metadata records ledger state changes (what
 *  `NFTokenPage` objects looked like before and after) but does not
 *  annotate which token was added, traded, or involved in a cancelled
 *  offer. The functions here bridge that gap so API consumers don't have
 *  to perform the same inference themselves.
 *
 *  These helpers are also consumed directly by Clio (the XRPL History
 *  API server), which performs the same enrichment independently of
 *  rippled. That is why the helpers are free functions rather than
 *  file-scope statics.
 */

#include <xrpl/protocol/NFTokenID.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/TxMeta.h>
#include <xrpl/protocol/jss.h>

#include <algorithm>
#include <iterator>
#include <memory>
#include <optional>
#include <vector>

namespace xrpl {

/** Returns true if this transaction could have produced or consumed an NFToken.
 *
 *  Guards all downstream extraction logic. A transaction qualifies only when
 *  it is one of the three NFT transaction types
 *  (`ttNFTOKEN_MINT`, `ttNFTOKEN_ACCEPT_OFFER`, `ttNFTOKEN_CANCEL_OFFER`)
 *  and its result code is `tesSUCCESS`. A failed transaction cannot have
 *  mutated any NFToken page, so metadata diffing would be meaningless.
 *
 *  @param serializedTx      The executed transaction; a null pointer yields
 *      false immediately.
 *  @param transactionMeta   Metadata from the same transaction, used to
 *      check the result code.
 *  @return True only when `serializedTx` is non-null, its type is an NFT
 *      transaction type, and the result is `tesSUCCESS`.
 */
bool
canHaveNFTokenID(std::shared_ptr<STTx const> const& serializedTx, TxMeta const& transactionMeta)
{
    if (!serializedTx)
        return false;

    TxType const tt = serializedTx->getTxnType();
    if (tt != ttNFTOKEN_MINT && tt != ttNFTOKEN_ACCEPT_OFFER && tt != ttNFTOKEN_CANCEL_OFFER)
        return false;

    // if the transaction failed nothing could have been delivered.
    if (!isTesSuccess(transactionMeta.getResultTER()))
        return false;

    return true;
}

/** Recovers the ID of the NFToken added by a mint transaction.
 *
 *  `ttNFTOKEN_MINT` metadata records the full token arrays of every
 *  affected `NFTokenPage` in `sfPreviousFields` and `sfFinalFields`, but
 *  does not mark the newly inserted entry. This function recovers it via
 *  set-difference: it accumulates all token IDs from previous states into
 *  `prevIDs` and all token IDs from final states into `finalIDs`, then
 *  uses `std::mismatch` to locate the first entry present in `finalIDs`
 *  but absent from `prevIDs`.
 *
 *  The invariant `finalIDs.size() == prevIDs.size() + 1` must hold exactly
 *  (tokens are minted one at a time). If it is violated the function
 *  returns `std::nullopt` rather than guessing.
 *
 *  @note When a mint causes an existing page to split, the linked-list
 *      rewiring may produce a `sfModifiedNode` for a third page whose
 *      `sfPreviousFields` contain only pointer updates (`NextPageMin` /
 *      `PreviousPageMin`) with no `sfNFTokens` array. Such nodes are
 *      skipped silently; without this guard the size invariant would
 *      incorrectly fail for legitimate mints.
 *
 *  @param transactionMeta  Metadata from a `ttNFTOKEN_MINT` transaction.
 *  @return The `uint256` ID of the newly minted token, or `std::nullopt`
 *      if the size invariant is violated or `std::mismatch` unexpectedly
 *      reaches the end of `finalIDs`.
 */
std::optional<uint256>
getNFTokenIDFromPage(TxMeta const& transactionMeta)
{
    std::vector<uint256> prevIDs;
    std::vector<uint256> finalIDs;

    for (STObject const& node : transactionMeta.getNodes())
    {
        if (node.getFieldU16(sfLedgerEntryType) != ltNFTOKEN_PAGE)
            continue;

        SField const& fName = node.getFName();
        if (fName == sfCreatedNode)
        {
            STArray const& toAddPrevNFTs =
                node.peekAtField(sfNewFields).downcast<STObject>().getFieldArray(sfNFTokens);
            std::ranges::transform(
                toAddPrevNFTs, std::back_inserter(finalIDs), [](STObject const& nft) {
                    return nft.getFieldH256(sfNFTokenID);
                });
        }
        else if (fName == sfModifiedNode)
        {
            // When a mint results in splitting an existing page,
            // it results in a created page and a modified node. Sometimes,
            // the created node needs to be linked to a third page, resulting
            // in modifying that third page's PreviousPageMin or NextPageMin
            // field changing, but no NFTs within that page changing. In this
            // case, there will be no previous NFTs and we need to skip.
            // However, there will always be NFTs listed in the final fields,
            // as xrpld outputs all fields in final fields even if they were
            // not changed.
            STObject const& previousFields =
                node.peekAtField(sfPreviousFields).downcast<STObject>();
            if (!previousFields.isFieldPresent(sfNFTokens))
                continue;

            STArray const& toAddPrevNFTs = previousFields.getFieldArray(sfNFTokens);
            std::ranges::transform(
                toAddPrevNFTs, std::back_inserter(prevIDs), [](STObject const& nft) {
                    return nft.getFieldH256(sfNFTokenID);
                });

            STArray const& toAddFinalNFTs =
                node.peekAtField(sfFinalFields).downcast<STObject>().getFieldArray(sfNFTokens);
            std::ranges::transform(
                toAddFinalNFTs, std::back_inserter(finalIDs), [](STObject const& nft) {
                    return nft.getFieldH256(sfNFTokenID);
                });
        }
    }

    // We expect NFTs to be added one at a time.  So finalIDs should be one
    // longer than prevIDs.  If that's not the case something is messed up.
    if (finalIDs.size() != prevIDs.size() + 1)
        return std::nullopt;

    auto const diff = std::ranges::mismatch(finalIDs, prevIDs);

    // There should always be a difference so the returned finalIDs
    // iterator should never be end().  But better safe than sorry.
    if (diff.in1 == finalIDs.end())
        return std::nullopt;

    return *diff.in1;
}

/** Collects the NFToken IDs referenced by deleted `NFTokenOffer` objects.
 *
 *  Both `ttNFTOKEN_ACCEPT_OFFER` and `ttNFTOKEN_CANCEL_OFFER` delete one
 *  or more `ltNFTOKEN_OFFER` ledger entries. Each deleted offer's
 *  `sfFinalFields` carries the `sfNFTokenID` it was created for, so the
 *  token identity is recoverable without any set-difference arithmetic.
 *
 *  The result is sorted and deduplicated because `ttNFTOKEN_CANCEL_OFFER`
 *  can cancel multiple offers simultaneously, and several offers may
 *  reference the same NFT.
 *
 *  @param transactionMeta  Metadata from a `ttNFTOKEN_ACCEPT_OFFER` or
 *      `ttNFTOKEN_CANCEL_OFFER` transaction.
 *  @return Sorted, deduplicated vector of `uint256` NFToken IDs recovered
 *      from all deleted offer nodes. Empty if no qualifying deletions are
 *      found.
 */
std::vector<uint256>
getNFTokenIDFromDeletedOffer(TxMeta const& transactionMeta)
{
    std::vector<uint256> tokenIDResult;
    for (STObject const& node : transactionMeta.getNodes())
    {
        if (node.getFieldU16(sfLedgerEntryType) != ltNFTOKEN_OFFER ||
            node.getFName() != sfDeletedNode)
            continue;

        auto const& toAddNFT =
            node.peekAtField(sfFinalFields).downcast<STObject>().getFieldH256(sfNFTokenID);
        tokenIDResult.push_back(toAddNFT);
    }

    std::ranges::sort(tokenIDResult);
    auto const uniq = std::ranges::unique(tokenIDResult);
    tokenIDResult.erase(uniq.begin(), uniq.end());
    return tokenIDResult;
}

/** Injects synthetic NFToken ID field(s) into an RPC transaction response.
 *
 *  Dispatches to the appropriate extraction helper based on transaction type
 *  and writes into `response`:
 *
 *  - `ttNFTOKEN_MINT` — writes `jss::nftoken_id` (single string) via
 *    `getNFTokenIDFromPage`.
 *  - `ttNFTOKEN_ACCEPT_OFFER` — writes `jss::nftoken_id` (single string,
 *    first element) via `getNFTokenIDFromDeletedOffer`.
 *  - `ttNFTOKEN_CANCEL_OFFER` — writes `jss::nftoken_ids` (JSON array of
 *    all deduplicated token IDs) via `getNFTokenIDFromDeletedOffer`.
 *
 *  All failure modes are silent: if `canHaveNFTokenID` returns false, or
 *  if extraction yields no result, the function returns without adding any
 *  field. No exceptions are thrown.
 *
 *  In practice this is called from `insertNFTSyntheticInJson` in
 *  `NFTSyntheticSerializer.cpp`, which targets `response[jss::meta]`.
 *
 *  @param response         The JSON object to enrich; fields are written
 *      directly into it (caller is responsible for scoping to `jss::meta`).
 *  @param transaction      The executed transaction. Null is handled
 *      gracefully via `canHaveNFTokenID`.
 *  @param transactionMeta  Read-only metadata used for both eligibility
 *      checking and token ID extraction.
 */
void
insertNFTokenID(
    json::Value& response,
    std::shared_ptr<STTx const> const& transaction,
    TxMeta const& transactionMeta)
{
    if (!canHaveNFTokenID(transaction, transactionMeta))
        return;

    if (auto const type = transaction->getTxnType(); type == ttNFTOKEN_MINT)
    {
        std::optional<uint256> result = getNFTokenIDFromPage(transactionMeta);
        if (result.has_value())
            response[jss::nftoken_id] = to_string(result.value());
    }
    else if (type == ttNFTOKEN_ACCEPT_OFFER)
    {
        std::vector<uint256> result = getNFTokenIDFromDeletedOffer(transactionMeta);

        if (!result.empty())
            response[jss::nftoken_id] = to_string(result.front());
    }
    else if (type == ttNFTOKEN_CANCEL_OFFER)
    {
        std::vector<uint256> const result = getNFTokenIDFromDeletedOffer(transactionMeta);

        response[jss::nftoken_ids] = json::Value(json::ValueType::Array);
        for (auto const& nftID : result)
            response[jss::nftoken_ids].append(to_string(nftID));
    }
}

}  // namespace xrpl
