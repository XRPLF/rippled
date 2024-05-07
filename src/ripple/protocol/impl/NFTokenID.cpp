//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/protocol/NFTokenID.h>
#include <ripple/protocol/jss.h>

namespace ripple {

bool
canHaveNFTokenID(
    std::shared_ptr<STTx const> const& serializedTx,
    TxMeta const& transactionMeta)
{
    if (!serializedTx)
        return false;

    TxType const tt = serializedTx->getTxnType();
    if (tt != ttNFTOKEN_MINT && tt != ttNFTOKEN_ACCEPT_OFFER &&
        tt != ttNFTOKEN_CANCEL_OFFER)
        return false;

    // if the transaction failed nothing could have been delivered.
    if (transactionMeta.getResultTER() != tesSUCCESS)
        return false;

    return true;
}

std::optional<uint256>
getNFTokenIDFromPage(TxMeta const& transactionMeta)
{
    // The metadata does not make it obvious which NFT was added.  To figure
    // that out we gather up all of the previous NFT IDs and all of the final
    // NFT IDs and compare them to find what changed.
    std::vector<uint256> prevIDs;
    std::vector<uint256> finalIDs;

    for (STObject const& node : transactionMeta.getNodes())
    {
        if (node.getFieldU16(sfLedgerEntryType) != ltNFTOKEN_PAGE)
            continue;

        SField const& fName = node.getFName();
        if (fName == sfCreatedNode)
        {
            STArray const& toAddPrevNFTs = node.peekAtField(sfNewFields)
                                               .downcast<STObject>()
                                               .getFieldArray(sfNFTokens);
            std::transform(
                toAddPrevNFTs.begin(),
                toAddPrevNFTs.end(),
                std::back_inserter(finalIDs),
                [](STObject const& nft) {
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
            // as rippled outputs all fields in final fields even if they were
            // not changed.
            STObject const& previousFields =
                node.peekAtField(sfPreviousFields).downcast<STObject>();
            if (!previousFields.isFieldPresent(sfNFTokens))
                continue;

            STArray const& toAddPrevNFTs =
                previousFields.getFieldArray(sfNFTokens);
            std::transform(
                toAddPrevNFTs.begin(),
                toAddPrevNFTs.end(),
                std::back_inserter(prevIDs),
                [](STObject const& nft) {
                    return nft.getFieldH256(sfNFTokenID);
                });

            STArray const& toAddFinalNFTs = node.peekAtField(sfFinalFields)
                                                .downcast<STObject>()
                                                .getFieldArray(sfNFTokens);
            std::transform(
                toAddFinalNFTs.begin(),
                toAddFinalNFTs.end(),
                std::back_inserter(finalIDs),
                [](STObject const& nft) {
                    return nft.getFieldH256(sfNFTokenID);
                });
        }
    }

    // We expect NFTs to be added one at a time.  So finalIDs should be one
    // longer than prevIDs.  If that's not the case something is messed up.
    if (finalIDs.size() != prevIDs.size() + 1)
        return std::nullopt;

    // Find the first NFT ID that doesn't match.  We're looking for an
    // added NFT, so the one we want will be the mismatch in finalIDs.
    auto const diff = std::mismatch(
        finalIDs.begin(), finalIDs.end(), prevIDs.begin(), prevIDs.end());

    // There should always be a difference so the returned finalIDs
    // iterator should never be end().  But better safe than sorry.
    if (diff.first == finalIDs.end())
        return std::nullopt;

    return *diff.first;
}

std::vector<uint256>
getNFTokenIDFromDeletedOffer(TxMeta const& transactionMeta)
{
    std::vector<uint256> tokenIDResult;
    for (STObject const& node : transactionMeta.getNodes())
    {
        if (node.getFieldU16(sfLedgerEntryType) != ltNFTOKEN_OFFER ||
            node.getFName() != sfDeletedNode)
            continue;

        auto const& toAddNFT = node.peekAtField(sfFinalFields)
                                   .downcast<STObject>()
                                   .getFieldH256(sfNFTokenID);
        tokenIDResult.push_back(toAddNFT);
    }

    // Deduplicate the NFT IDs because multiple offers could affect the same NFT
    // and hence we would get duplicate NFT IDs
    sort(tokenIDResult.begin(), tokenIDResult.end());
    tokenIDResult.erase(
        unique(tokenIDResult.begin(), tokenIDResult.end()),
        tokenIDResult.end());
    return tokenIDResult;
}

void
insertNFTokenID(
    Json::Value& response,
    std::shared_ptr<STTx const> const& transaction,
    TxMeta const& transactionMeta)
{
    if (!canHaveNFTokenID(transaction, transactionMeta))
        return;

    // We extract the NFTokenID from metadata by comparing affected nodes
    if (auto const type = transaction->getTxnType(); type == ttNFTOKEN_MINT)
    {
        std::optional<uint256> result = getNFTokenIDFromPage(transactionMeta);
        if (result.has_value())
            response[jss::nftoken_id] = to_string(result.value());
    }
    else if (type == ttNFTOKEN_ACCEPT_OFFER)
    {
        std::vector<uint256> result =
            getNFTokenIDFromDeletedOffer(transactionMeta);

        if (result.size() > 0)
            response[jss::nftoken_id] = to_string(result.front());
    }
    else if (type == ttNFTOKEN_CANCEL_OFFER)
    {
        std::vector<uint256> result =
            getNFTokenIDFromDeletedOffer(transactionMeta);

        response[jss::nftoken_ids] = Json::Value(Json::arrayValue);
        for (auto const& nftID : result)
            response[jss::nftoken_ids].append(to_string(nftID));
    }
}

}  // namespace ripple
