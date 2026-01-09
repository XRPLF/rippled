#include <xrpld/rpc/Context.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/NetworkOPs.h>

namespace xrpl {

Json::Value
doFetchInfo(RPC::JsonContext& context)
{
    Json::Value ret(Json::objectValue);

    if (context.params.isMember(jss::clear) &&
        context.params[jss::clear].asBool())
    {
        context.netOps.clearLedgerFetch();
        ret[jss::clear] = true;
    }

    ret[jss::info] = context.netOps.getLedgerFetchInfo();

    return ret;
}

}  // namespace xrpl
