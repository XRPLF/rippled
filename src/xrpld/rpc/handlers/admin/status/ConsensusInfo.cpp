#include <xrpld/rpc/Context.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/NetworkOPs.h>
#include <xrpl/basics/TraceLog.h>

namespace xrpl {

json::Value
doConsensusInfo(RPC::JsonContext& context)
{
    TRACE_FUNC();
    json::Value ret(json::ObjectValue);

    ret[jss::info] = context.netOps.getConsensusInfo();

    return ret;
}

}  // namespace xrpl
