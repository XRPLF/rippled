/** @file
 *  Implements the `connect` admin RPC handler, which instructs the node to
 *  open an outbound peer connection to a caller-specified IP address and port.
 */

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

/** Initiate a manual outbound peer connection (ADMIN).
 *
 *  Validates the caller-supplied `ip` and optional `port`, then delegates to
 *  `Overlay::connect()`, which is non-blocking — the TCP handshake proceeds
 *  asynchronously and the response is returned immediately.
 *
 *  Request fields:
 *  - `ip`   (string, required) — IPv4 or IPv6 address of the target peer.
 *  - `port` (integer, optional) — target port; defaults to `kDEFAULT_PEER_PORT`
 *      (2459) when absent.
 *
 *  @param context  RPC dispatch context carrying the request parameters and
 *      application references.
 *  @return A JSON string value confirming the connection attempt, or a JSON
 *      error object if validation fails.
 *
 *  @note Returns `rpcNOT_SYNCED` when the node is running in standalone mode,
 *      where peer connections are not meaningful.
 *  @note If `ip` does not parse as a valid address, `Overlay::connect()` is
 *      silently skipped — the response still reads "attempting connection …"
 *      with no indication that the address was rejected. DNS names are not
 *      resolved.
 *  @note The port value is not range-checked; values outside `[1, 65535]` are
 *      passed through to the overlay unchanged.
 */
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
        iPort = kDEFAULT_PEER_PORT;
    }

    auto const ipStr = context.params[jss::ip].asString();
    auto ip = beast::IP::Endpoint::fromString(ipStr);

    if (!isUnspecified(ip))
        context.app.getOverlay().connect(ip.atPort(iPort));

    return RPC::makeObjectValue(
        "attempting connection to IP:" + ipStr + " port: " + std::to_string(iPort));
}

}  // namespace xrpl
