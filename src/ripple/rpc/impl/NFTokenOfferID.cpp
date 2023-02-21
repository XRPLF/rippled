//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2019 Ripple Labs Inc.

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

#include <ripple/rpc/NFTokenOfferID.h>

#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/OpenLedger.h>
#include <ripple/app/misc/Transaction.h>
#include <ripple/ledger/View.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/AccountID.h>
#include <ripple/protocol/Feature.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <boost/algorithm/string/case_conv.hpp>

namespace ripple {
namespace RPC {

bool
canHaveNFTokenOfferID(
    std::shared_ptr<STTx const> const& serializedTx,
    TxMeta const& transactionMeta)
{
    if (!serializedTx)
        return false;

    TxType const tt{serializedTx->getTxnType()};
    if (tt != ttNFTOKEN_CREATE_OFFER)
        return false;

    // if the transaction failed nothing could have been delivered.
    if (transactionMeta.getResultTER() != tesSUCCESS)
        return false;

    return true;
}

void
getOfferIDFromCreatedOffer(
    TxMeta const& transactionMeta,
    std::vector<uint256>& idResult)
{
    for (STObject const& node : transactionMeta.getNodes())
    {
        if (node.getFieldU16(sfLedgerEntryType) != ltNFTOKEN_OFFER)
            continue;

        if (node.getFName() == sfCreatedNode)
        {
            auto const& offerID = node.getFieldH256(sfLedgerIndex);
            idResult.push_back(offerID);
        }
    }
}

void
insertNFTokenOfferID(
    Json::Value& response,
    std::shared_ptr<STTx const> const& transaction,
    TxMeta const& transactionMeta)
{
    if (!canHaveNFTokenOfferID(transaction, transactionMeta))
        return;

    std::vector<uint256> offerIDResult;
    getOfferIDFromCreatedOffer(transactionMeta, offerIDResult);

    if (offerIDResult.size() == 1)
        response[jss::offer_id] = to_string(offerIDResult.front());
}

}  // namespace RPC
}  // namespace ripple
