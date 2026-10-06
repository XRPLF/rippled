#include <xrpld/rpc/Context.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/NetworkOPs.h>

namespace xrpl {

json::Value
doFetchInfo(rpc::JsonContext& context)
{
    json::Value ret(json::ValueType::Object);

    if (context.params.isMember(jss::clear))
    {
        if (!context.params[jss::clear].isBool())
            return rpcError(RpcInvalidParams);

        if (context.params[jss::clear].asBool())
        {
            context.netOps.clearLedgerFetch();
            ret[jss::clear] = true;
        }
    }

    ret[jss::info] = context.netOps.getLedgerFetchInfo();

    return ret;
}

}  // namespace xrpl
