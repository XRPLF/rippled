#include <xrpld/rpc/Context.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/NetworkOPs.h>

namespace xrpl {

json::Value
doNodeStall(RPC::JsonContext& context)
{
    if (context.role != Role::ADMIN)
        return rpcError(RpcNoPermission);

    if (context.params.isMember("clear") && context.params["clear"].asBool())
    {
        context.netOps.clearStall();

        json::Value jvResult;
        jvResult[jss::status] = "success";
        jvResult["stalled"] = false;
        return jvResult;
    }

    std::int64_t durationMs = 30000;
    if (context.params.isMember("duration_ms") && context.params["duration_ms"].isIntegral())
    {
        durationMs = context.params["duration_ms"].asInt();
        if (durationMs <= 0)
            return rpcError(RpcInvalidParams);
    }

    context.netOps.setStall(std::chrono::milliseconds(durationMs));

    json::Value jvResult;
    jvResult[jss::status] = "success";
    jvResult["stalled"] = true;
    jvResult["duration_ms"] = static_cast<int>(durationMs);
    return jvResult;
}

}  // namespace xrpl
