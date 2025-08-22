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

#include <xrpld/rpc/DeliveredAmount.h>
#include <xrpld/rpc/MPTokenIssuanceID.h>
#include <xrpld/rpc/detail/SyntheticFields.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/NFTSyntheticSerializer.h>
#include <xrpl/protocol/jss.h>

namespace ripple {
namespace RPC {

void
insertAllSyntheticInJson(
    Json::Value& response,
    ReadView const& ledger,
    std::shared_ptr<STTx const> const& transaction,
    TxMeta const& transactionMeta)
{
    insertDeliveredAmount(
        response[jss::meta], ledger, transaction, transactionMeta);
    insertNFTSyntheticInJson(response, transaction, transactionMeta);
    insertMPTokenIssuanceID(response[jss::meta], transaction, transactionMeta);
}

void
insertAllSyntheticInJson(
    Json::Value& response,
    JsonContext const& context,
    std::shared_ptr<STTx const> const& transaction,
    TxMeta const& transactionMeta)
{
    insertDeliveredAmount(
        response[jss::meta], context, transaction, transactionMeta);
    insertNFTSyntheticInJson(response, transaction, transactionMeta);
    insertMPTokenIssuanceID(response[jss::meta], transaction, transactionMeta);
}

}  // namespace RPC
}  // namespace ripple