//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2022 Ripple Labs Inc.

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

#include <ripple/app/main/Application.h>
#include <ripple/json/json_value.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/ledger/View.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/jss.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <ripple/rpc/impl/Tuning.h>

namespace ripple {

static void
appendCFTHolderJson(
    Application const& app,
    std::shared_ptr<SLE const> const& cft,
    Json::Value& holders)
{
    Json::Value& obj(holders.append(Json::objectValue));

    obj[jss::cftoken_index] = to_string(cft->key());
    obj[jss::flags] = (*cft)[sfFlags];
    obj[jss::account] = toBase58(cft->getAccountID(sfAccount));
    obj[jss::cft_amount] = STUInt64{(*cft)[sfCFTAmount]}.getJson(JsonOptions::none);
    
    if((*cft)[sfLockedAmount])
        obj[jss::locked_amount] = STUInt64{(*cft)[sfLockedAmount]}.getJson(JsonOptions::none);
}

// {
//   cft_issuance_id: <token hash>
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
//   limit: integer                 // optional
//   marker: opaque                 // optional, resume previous query
// }
static Json::Value
enumerateCFTHolders(
    RPC::JsonContext& context,
    uint192 const& cftIssuanceID,
    Keylet const& directory)
{
    unsigned int limit;
    if (auto err = readLimitField(limit, RPC::Tuning::cftHolders, context))
        return *err;

    std::shared_ptr<ReadView const> ledger;

    if (auto result = RPC::lookupLedger(ledger, context); !ledger)
        return result;

    if (!ledger->exists(directory))
        return rpcError(rpcOBJECT_NOT_FOUND);

    Json::Value result;
    result[jss::cft_issuance_id] = to_string(cftIssuanceID);

    Json::Value& jsonHolders(result[jss::holders] = Json::arrayValue);

    std::vector<std::shared_ptr<SLE const>> holders;
    unsigned int reserve(limit);
    uint256 startAfter;
    std::uint64_t startHint = 0;

    if (context.params.isMember(jss::marker))
    {
        // We have a start point. Use limit - 1 from the result and use the
        // very last one for the resume.
        Json::Value const& marker(context.params[jss::marker]);

        if (!marker.isString())
            return RPC::expected_field_error(jss::marker, "string");

        if (!startAfter.parseHex(marker.asString()))
            return rpcError(rpcINVALID_PARAMS);

        auto const sle = ledger->read(keylet::cftoken(startAfter));

        if (!sle || cftIssuanceID != sle->getFieldH192(sfCFTokenIssuanceID))
            return rpcError(rpcINVALID_PARAMS);

        startHint = sle->getFieldU64(sfCFTokenNode);
        appendCFTHolderJson(context.app, sle, jsonHolders);
        holders.reserve(reserve);
    }
    else
    {
        // We have no start point, limit should be one higher than requested.
        holders.reserve(++reserve);
    }

    if (!forEachItemAfter(
            *ledger,
            directory,
            startAfter,
            startHint,
            reserve,
            [&holders](std::shared_ptr<SLE const> const& cftoken) {
                if (cftoken->getType() == ltCFTOKEN)
                {
                    holders.emplace_back(cftoken);
                    return true;
                }

                return false;
            }))
    {
        return rpcError(rpcINVALID_PARAMS);
    }

    if (holders.size() == reserve)
    {
        result[jss::limit] = limit;
        result[jss::marker] = to_string(holders.back()->key());
        holders.pop_back();
    }

    for (auto const& cft : holders)
        appendCFTHolderJson(context.app, cft, jsonHolders);

    context.loadType = Resource::feeMediumBurdenRPC;
    return result;
}

Json::Value
doCFTHolders(RPC::JsonContext& context)
{
    if (!context.params.isMember(jss::cft_issuance_id))
        return RPC::missing_field_error(jss::cft_issuance_id);

    uint192 cftIssuanceID;

    if (!cftIssuanceID.parseHex(context.params[jss::cft_issuance_id].asString()))
        return RPC::invalid_field_error(jss::cft_issuance_id);

    return enumerateCFTHolders(context, cftIssuanceID, keylet::cft_dir(cftIssuanceID));
}

}  // namespace ripple
