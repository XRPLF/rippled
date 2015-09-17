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

Json::Value doFeature (RPC::Context& context)
{
    if (!context.params.isMember (jss::feature))
    {
        Json::Value jvReply = Json::objectValue;
        jvReply[jss::features] = context.app.getAmendmentTable ().getJson(0);
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

    if (!context.params.isMember (jss::vote))
        return context.app.getAmendmentTable ().getJson(uFeature);

    // WRITEME
    return rpcError (rpcNOT_SUPPORTED);
}


} // ripple
