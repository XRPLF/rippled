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


namespace ripple {

// {
//   node: <domain>|<public_key>
// }
Json::Value RPCHandler::doUnlDelete (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    if (!params.isMember ("node"))
        return rpcError (rpcINVALID_PARAMS);

    std::string strNode     = params["node"].asString ();

    RippleAddress   raNodePublic;

    if (raNodePublic.setNodePublic (strNode))
    {
        getApp().getUNL ().nodeRemovePublic (raNodePublic);

        return "removing node by public key";
    }
    else
    {
        getApp().getUNL ().nodeRemoveDomain (strNode);

        return "removing node by domain";
    }
}

} // ripple
