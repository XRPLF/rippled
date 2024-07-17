//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2014 Ripple Labs Inc.

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

#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/AmendmentTable.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/RPCErr.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/Context.h>

namespace ripple {

// {
//   feature : <feature>
//   vetoed : true/false
// }
Json::Value
doFeature(RPC::JsonContext& context)
{
    if (context.app.config().reporting())
        return rpcError(rpcREPORTING_UNSUPPORTED);

    bool const isAdmin = context.role == Role::ADMIN;
    // Get majority amendment status
    majorityAmendments_t majorities;

    if (auto const valLedger = context.ledgerMaster.getValidatedLedger())
        majorities = getMajorityAmendments(*valLedger);

    auto& table = context.app.getAmendmentTable();

    if (!context.params.isMember(jss::feature))
    {
        auto features = table.getJson(isAdmin);

        for (auto const& [h, t] : majorities)
        {
            features[to_string(h)][jss::majority] =
                t.time_since_epoch().count();
        }

        Json::Value jvReply = Json::objectValue;
        jvReply[jss::features] = features;
        return jvReply;
    }

    auto feature = table.find(context.params[jss::feature].asString());

    // If the feature is not found by name, try to parse the `feature` param as
    // a feature ID. If that fails, return an error.
    if (!feature && !feature.parseHex(context.params[jss::feature].asString()))
        return rpcError(rpcBAD_FEATURE);

    if (context.params.isMember(jss::vetoed))
    {
        if (!isAdmin)
            return rpcError(rpcNO_PERMISSION);

        if (context.params[jss::vetoed].asBool())
            table.veto(feature);
        else
            table.unVeto(feature);
    }

    Json::Value jvReply = table.getJson(feature, isAdmin);
    if (!jvReply)
        return rpcError(rpcBAD_FEATURE);

    auto m = majorities.find(feature);
    if (m != majorities.end())
        jvReply[jss::majority] = m->second.time_since_epoch().count();

    return jvReply;
}

}  // namespace ripple
