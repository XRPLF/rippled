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

#include <xrpld/rpc/MPTokenIssuanceID.h>

#include <xrpld/app/misc/Transaction.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/RPCErr.h>

namespace ripple {

namespace RPC {

bool
canHaveMPTokenIssuanceID(
    std::shared_ptr<STTx const> const& serializedTx,
    TxMeta const& transactionMeta)
{
    if (!serializedTx)
        return false;

    TxType const tt = serializedTx->getTxnType();
    if (tt != ttMPTOKEN_ISSUANCE_CREATE)
        return false;

    // if the transaction failed nothing could have been delivered.
    if (transactionMeta.getResultTER() != tesSUCCESS)
        return false;

    return true;
}

std::optional<uint192>
getIDFromCreatedIssuance(TxMeta const& transactionMeta)
{
    for (STObject const& node : transactionMeta.getNodes())
    {
        if (node.getFieldU16(sfLedgerEntryType) != ltMPTOKEN_ISSUANCE ||
            node.getFName() != sfCreatedNode)
            continue;

        auto const& mptNode =
            node.peekAtField(sfNewFields).downcast<STObject>();
        return getMptID(
            mptNode.getAccountID(sfIssuer), mptNode.getFieldU32(sfSequence));
    }

    return std::nullopt;
}

void
insertMPTokenIssuanceID(
    Json::Value& response,
    std::shared_ptr<STTx const> const& transaction,
    TxMeta const& transactionMeta)
{
    if (!canHaveMPTokenIssuanceID(transaction, transactionMeta))
        return;

    std::optional<uint192> result = getIDFromCreatedIssuance(transactionMeta);
    if (result.has_value())
        response[jss::mpt_issuance_id] = to_string(result.value());
}

}  // namespace RPC
}  // namespace ripple
