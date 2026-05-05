#include <xrpld/rpc/Context.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/NetworkOPs.h>

namespace xrpl {

json::Value
doConsensusInfo(RPC::JsonContext& context)
{
    json::Value ret(json::ObjectValue);

    ret[jss::info] = context.netOps.getConsensusInfo();

    return ret;
}

}  // namespace xrpl
