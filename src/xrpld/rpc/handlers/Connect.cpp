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

#include <xrpld/app/main/Application.h>
#include <xrpld/core/Config.h>
#include <xrpld/overlay/Overlay.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/Handler.h>

#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

// {
//   ip: <string>,
//   port: <number>
// }
// XXX Might allow domain for manual connections.
Json::Value
doConnect(RPC::JsonContext& context)
{
    if (context.app.config().standalone())
    {
        return RPC::make_error(rpcNOT_SYNCED);
    }

    if (!context.params.isMember(jss::ip))
        return RPC::missing_field_error(jss::ip);

    if (context.params.isMember(jss::port) &&
        !context.params[jss::port].isConvertibleTo(Json::intValue))
    {
        return rpcError(rpcINVALID_PARAMS);
    }

    int iPort;

    if (context.params.isMember(jss::port))
        iPort = context.params[jss::port].asInt();
    else
        iPort = DEFAULT_PEER_PORT;

    auto const ip_str = context.params[jss::ip].asString();
    auto ip = beast::IP::Endpoint::from_string(ip_str);

    if (!is_unspecified(ip))
        context.app.overlay().connect(ip.at_port(iPort));

    return RPC::makeObjectValue(
        "attempting connection to IP:" + ip_str +
        " port: " + std::to_string(iPort));
}

}  // namespace ripple
