/**
 * @file BlackList.cpp
 * @brief Admin RPC handler that surfaces resource-consumption accounting
 *     to node operators.
 *
 * Implements the `"blacklist"` admin RPC command. The handler queries the
 * global `Resource::Manager` for all tracked endpoints (peers and clients)
 * whose accumulated load balance meets or exceeds a caller-supplied threshold,
 * making them visible to operators as candidates for rate-limiting or
 * disconnection.
 *
 * Access is restricted to `Role::ADMIN`; enforcement is handled by the RPC
 * dispatch layer before this handler is ever invoked.
 */

#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/ResourceManager.h>

namespace xrpl {

/**
 * @brief Report endpoints that have exceeded a resource-consumption threshold.
 *
 * Delegates entirely to `Resource::Manager::getJson`. If the request contains
 * a `threshold` field it is forwarded as the filter; otherwise the manager's
 * default of `kWARNING_THRESHOLD` (5000) is used, which covers every endpoint
 * already in warning territory.
 *
 * Each emitted entry reports the local balance, remote balance (propagated via
 * peer gossip), and connection type. Passing `threshold = 0` returns every
 * tracked endpoint regardless of load; raising it toward the disconnect
 * threshold (25000) narrows the view to the most severely misbehaving
 * endpoints.
 *
 * @param context  RPC dispatch context; `context.params` may contain an
 *     optional integer `threshold` field that overrides the default filter.
 * @return A JSON object produced by `Resource::Manager::getJson`, containing
 *     all inbound entries whose combined balance meets or exceeds the
 *     effective threshold.
 * @note If `threshold` is present but not convertible to an integer,
 *     `Json::Value::asInt()` throws `Json::LogicError`. No range check is
 *     performed — negative values are accepted and will return every tracked
 *     endpoint — but this is harmless given the admin-only access gate.
 */
json::Value
doBlackList(RPC::JsonContext& context)
{
    auto& rm = context.app.getResourceManager();
    if (context.params.isMember(jss::threshold))
    {
        return rm.getJson(context.params[jss::threshold].asInt());
    }

    return rm.getJson();
}

}  // namespace xrpl
