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
doConnect(RPC::JsonContext& context)
{
    if (context.app.config().standalone())
    {
        return RPC::makeError(RpcNotSynced);
    }

    if (!context.params.isMember(jss::ip))
        return RPC::missingFieldError(jss::ip);

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
    auto ip = beast::IP::Endpoint::fromString(ipStr);

    if (!isUnspecified(ip))
        context.app.getOverlay().connect(ip.atPort(iPort));

    return RPC::makeObjectValue(
        "attempting connection to IP:" + ipStr + " port: " + std::to_string(iPort));
}

}  // namespace xrpl
