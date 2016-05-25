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
#include <ripple/core/Config.h>
#include <ripple/net/RPCErr.h>
#include <ripple/overlay/Overlay.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/impl/Handler.h>
#include <ripple/basics/make_lock.h>

namespace ripple {

// {
//   ip: <string>,
//   port: <number>
// }
// XXX Might allow domain for manual connections.
Json::Value doConnect (RPC::Context& context)
{
    auto lock = make_lock(context.app.getMasterMutex());
    if (context.app.config().standalone())
        return "cannot connect in standalone mode";

    if (!context.params.isMember (jss::ip))
        return RPC::missing_field_error (jss::ip);

    if (context.params.isMember (jss::port) &&
        !context.params[jss::port].isConvertibleTo (Json::intValue))
    {
        return rpcError (rpcINVALID_PARAMS);
    }

    int iPort;

    if(context.params.isMember (jss::port))
        iPort = context.params[jss::port].asInt ();
    else
        iPort = 6561;

    auto ip = beast::IP::Endpoint::from_string(
        context.params[jss::ip].asString ());

    if (! is_unspecified (ip))
        context.app.overlay ().connect (ip.at_port(iPort));

    return RPC::makeObjectValue ("connecting");
}

} // ripple
