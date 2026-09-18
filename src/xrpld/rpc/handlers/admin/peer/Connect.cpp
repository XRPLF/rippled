#include <xrpld/app/main/Application.h>
#include <xrpld/core/Config.h>
#include <xrpld/overlay/Overlay.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/Handler.h>

#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/protocol/jss.h>

#include <string>

namespace xrpl {

// {
//   ip: <string>,
//   port: <number>
// }
// XXX Might allow domain for manual connections.
json::Value
doConnect(rpc::JsonContext& context)
{
    if (context.app.config().standalone())
    {
        return rpc::makeError(RpcNotSynced);
    }

    if (!context.params.isMember(jss::ip))
        return rpc::missingFieldError(jss::ip);

    // Without this, asString() below throws for an array or an object, and
    // silently stringifies every scalar into something that cannot parse as an
    // address.
    if (!context.params[jss::ip].isString())
        return RPC::expectedFieldError(jss::ip, "a string");

    if (context.params.isMember(jss::port) &&
        !context.params[jss::port].isConvertibleTo(json::ValueType::Int))
    {
        return rpcError(RpcInvalidParams);
    }

    int iPort = 0;

    if (context.params.isMember(jss::port))
    {
        iPort = context.params[jss::port].asInt();
    }
    else
    {
        iPort = kDefaultPeerPort;
    }

    auto const ipStr = context.params[jss::ip].asString();
    auto ip = beast::ip::Endpoint::fromString(ipStr);

    if (!isUnspecified(ip))
        context.app.getOverlay().connect(ip.atPort(iPort));

    return rpc::makeObjectValue(
        "attempting connection to IP:" + ipStr + " port: " + std::to_string(iPort));
}

}  // namespace xrpl
