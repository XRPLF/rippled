#include <xrpld/rpc/Context.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/NetworkOPs.h>
#include <xrpl/basics/TraceLog.h>

namespace xrpl {

json::Value
doFetchInfo(RPC::JsonContext& context)
{
    TRACE_FUNC();
    json::Value ret(json::ObjectValue);

    if (context.params.isMember(jss::clear) && context.params[jss::clear].asBool())
    {
        context.netOps.clearLedgerFetch();
        ret[jss::clear] = true;
    }

    ret[jss::info] = context.netOps.getLedgerFetchInfo();

    return ret;
}

}  // namespace xrpl
