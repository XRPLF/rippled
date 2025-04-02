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

Json::Value
doVaultInfo(RPC::JsonContext& context)
{
    std::shared_ptr<ReadView const> lpLedger;
    auto jvResult = RPC::lookupLedger(lpLedger, context);

    if (!lpLedger)
        return jvResult;

    auto const uNodeIndex =
        RPC::parseVault(context.params[jss::vault], jvResult)
            .value_or(beast::zero);
    if (uNodeIndex == beast::zero)
    {
        jvResult[jss::error] = "malformedRequest";
        return jvResult;
    }

    bool const isBinary = context.params[jss::binary].asBool();

    auto const sleVault = lpLedger->read(keylet::vault(uNodeIndex));
    auto const sleIssuance = sleVault == nullptr  //
        ? nullptr
        : lpLedger->read(keylet::mptIssuance(sleVault->at(sfShareMPTID)));
    if (!sleVault || !sleIssuance)
    {
        jvResult[jss::error] = "entryNotFound";
        return jvResult;
    }

    Json::Value directory = Json::objectValue;
    // Some directory positions in nodes are hardcoded below, because the
    // order of writing these is hardcoded, but it may not stay like this
    // forever. If a given type can have any number of nodes, use an array
    // rather than a number
    directory[jss::vault] = 0;
    directory[jss::mpt_issuance] = 1;

    Json::Value nodes = Json::arrayValue;
    if (!isBinary)
    {
        auto& vault = nodes.append(Json::objectValue);
        vault = sleVault->getJson(JsonOptions::none);

        auto& issuance = nodes.append(Json::objectValue);
        issuance = sleIssuance->getJson(JsonOptions::none);
    }
    else
    {
        auto& vault = nodes.append(Json::objectValue);
        vault[jss::data] = serializeHex(*sleVault);
        vault[jss::index] = to_string(sleVault->key());

        auto& issuance = nodes.append(Json::objectValue);
        issuance[jss::data] = serializeHex(*sleIssuance);
        issuance[jss::index] = to_string(sleIssuance->key());
    }

    jvResult[jss::directory] = directory;
    jvResult[jss::nodes] = nodes;
    return jvResult;
}

}  // namespace ripple
