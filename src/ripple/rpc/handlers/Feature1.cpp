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

#include <BeastConfig.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/AmendmentTable.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/net/RPCErr.h>
#include <ripple/rpc/Context.h>
#include <beast/module/core/text/LexicalCast.h>

namespace ripple {


// {
//   feature : <feature>
//   vetoed : true/false
// }
Json::Value doFeature (RPC::Context& context)
{

    // Get majority amendment status
    majorityAmendments_t majorities;
    {
        auto valLedger = context.ledgerMaster.getValidatedLedger();
        if (valLedger)
            majorities = getMajorityAmendments (*valLedger);
    }

    if (!context.params.isMember (jss::feature))
    {
        Json::Value jvReply = Json::objectValue;
        jvReply[jss::features] = context.app.getAmendmentTable ().getJson(0);

        for (auto const& m : majorities)
            jvReply[jss::features][to_string(m.first)][jss::majority]
                = m.second;

        return jvReply;
    }

    uint256 uFeature
            = context.app.getAmendmentTable ().get(
                context.params[jss::feature].asString());

    if (uFeature.isZero ())
    {
        uFeature.SetHex (context.params[jss::feature].asString ());

        if (uFeature.isZero ())
            return rpcError (rpcBAD_FEATURE);
    }

    if (context.params.isMember (jss::vetoed))
    {
        if (context.params[jss::vetoed].asBool ())
            context.app.getAmendmentTable().veto (uFeature);
        else
            context.app.getAmendmentTable().unVeto(uFeature);
    }

    Json::Value jvReply = context.app.getAmendmentTable ().getJson(uFeature);

    auto m = majorities.find (uFeature);
    if (m != majorities.end())
        jvReply [jss::majority] = m->second;

    return jvReply;
}


} // ripple
