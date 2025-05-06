//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCHelpers.h>

#include <xrpl/beast/utility/Zero.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/serialize.h>

namespace ripple {

static std::optional<uint256>
parseVault(Json::Value const& params, Json::Value& jvResult)
{
    auto const hasVaultId = params.isMember(jss::vault_id);
    auto const hasOwner = params.isMember(jss::owner);
    auto const hasSeq = params.isMember(jss::seq);

    uint256 uNodeIndex = beast::zero;
    if (hasVaultId && !hasOwner && !hasSeq)
    {
        if (!uNodeIndex.parseHex(params[jss::vault_id].asString()))
        {
            RPC::inject_error(rpcINVALID_PARAMS, jvResult);
            return std::nullopt;
        }
        // else uNodeIndex holds the value we need
    }
    else if (!hasVaultId && hasOwner && hasSeq)
    {
        auto const id = parseBase58<AccountID>(params[jss::owner].asString());
        if (!id)
        {
            RPC::inject_error(rpcACT_MALFORMED, jvResult);
            return std::nullopt;
        }
        else if (
            !(params[jss::seq].isInt() || params[jss::seq].isUInt()) ||
            params[jss::seq].asDouble() <= 0.0 ||
            params[jss::seq].asDouble() > double(Json::Value::maxUInt))
        {
            RPC::inject_error(rpcINVALID_PARAMS, jvResult);
            return std::nullopt;
        }

        uNodeIndex = keylet::vault(*id, params[jss::seq].asUInt()).key;
    }
    else
    {
        // Invalid combination of fields vault_id/owner/seq
        RPC::inject_error(rpcINVALID_PARAMS, jvResult);
        return std::nullopt;
    }

    return uNodeIndex;
}

Json::Value
doVaultInfo(RPC::JsonContext& context)
{
    std::shared_ptr<ReadView const> lpLedger;
    auto jvResult = RPC::lookupLedger(lpLedger, context);

    if (!lpLedger)
        return jvResult;

    auto const uNodeIndex =
        parseVault(context.params, jvResult).value_or(beast::zero);
    if (uNodeIndex == beast::zero)
    {
        jvResult[jss::error] = "malformedRequest";
        return jvResult;
    }

    auto const sleVault = lpLedger->read(keylet::vault(uNodeIndex));
    auto const sleIssuance = sleVault == nullptr  //
        ? nullptr
        : lpLedger->read(keylet::mptIssuance(sleVault->at(sfShareMPTID)));
    if (!sleVault || !sleIssuance)
    {
        jvResult[jss::error] = "entryNotFound";
        return jvResult;
    }

    Json::Value& vault = jvResult[jss::vault];
    vault = sleVault->getJson(JsonOptions::none);
    auto& share = vault[jss::shares];
    share = sleIssuance->getJson(JsonOptions::none);

    jvResult[jss::vault] = vault;
    return jvResult;
}

}  // namespace ripple
