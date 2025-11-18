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
