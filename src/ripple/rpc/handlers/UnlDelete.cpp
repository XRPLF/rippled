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
#include <ripple/app/misc/UniqueNodeList.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/impl/Handler.h>
#include <beast/utility/make_lock.h>

namespace ripple {

// {
//   node: <domain>|<public_key>
// }
Json::Value doUnlDelete (RPC::Context& context)
{
    auto lock = beast::make_lock(context.app.getMasterMutex());

    if (!context.params.isMember (jss::node))
        return rpcError (rpcINVALID_PARAMS);

    auto strNode = context.params[jss::node].asString ();
    RippleAddress raNodePublic;

    if (raNodePublic.setNodePublic (strNode))
    {
        context.app.getUNL ().nodeRemovePublic (raNodePublic);
        return RPC::makeObjectValue ("removing node by public key");
    }
    else
    {
        context.app.getUNL ().nodeRemoveDomain (strNode);
        return RPC::makeObjectValue ("removing node by domain");
    }
}

} // ripple
