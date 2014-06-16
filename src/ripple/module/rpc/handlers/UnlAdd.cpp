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
//   node: <domain>|<node_public>,
//   comment: <comment>             // optional
// }
Json::Value doUnlAdd (RPC::Context& context)
{
    std::string strNode     = context.params_.isMember ("node") ? context.params_["node"].asString () : "";
    std::string strComment  = context.params_.isMember ("comment") ? context.params_["comment"].asString () : "";

    RippleAddress   raNodePublic;

    if (raNodePublic.setNodePublic (strNode))
    {
        getApp().getUNL ().nodeAddPublic (raNodePublic, UniqueNodeList::vsManual, strComment);

        return "adding node by public key";
    }
    else
    {
        getApp().getUNL ().nodeAddDomain (strNode, UniqueNodeList::vsManual, strComment);

        return "adding node by domain";
    }
}

} // ripple
