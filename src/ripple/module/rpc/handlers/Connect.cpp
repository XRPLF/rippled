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

#include <ripple/overlay/Overlay.h>

namespace ripple {

// {
//   ip: <string>,
//   port: <number>
// }
// XXX Might allow domain for manual connections.
Json::Value doConnect (RPC::Context& context)
{
    if (getConfig ().RUN_STANDALONE)
        return "cannot connect in standalone mode";

    if (!context.params_.isMember ("ip"))
        return RPC::missing_field_error ("ip");

    if (context.params_.isMember ("port") && !context.params_["port"].isConvertibleTo (Json::intValue))
        return rpcError (rpcINVALID_PARAMS);

    int iPort;

    if(context.params_.isMember ("port"))
        iPort = context.params_["port"].asInt ();
    else
        iPort = SYSTEM_PEER_PORT;

    beast::IP::Endpoint ip (beast::IP::Endpoint::from_string(context.params_["ip"].asString ()));

    if (! is_unspecified (ip))
        getApp().overlay ().connect (ip.at_port(iPort));

    return "connecting";
}

} // ripple
