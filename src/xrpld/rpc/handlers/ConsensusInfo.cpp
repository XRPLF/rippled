#include <xrpld/app/misc/NetworkOPs.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

Json::Value
doConsensusInfo(RPC::JsonContext& context)
{
    Json::Value ret(Json::objectValue);

    ret[jss::info] = context.netOps.getConsensusInfo();

    return ret;
}

}  // namespace ripple
