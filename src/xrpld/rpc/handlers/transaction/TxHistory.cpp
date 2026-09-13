#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/DeliverMax.h>
#include <xrpld/app/misc/Transaction.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/Role.h>

#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/rdb/RelationalDatabase.h>
#include <xrpl/resource/Fees.h>

namespace xrpl {

// {
//   start: <index>
// }
json::Value
doTxHistory(rpc::JsonContext& context)
{
    if (!context.app.config().useTxTables())
        return rpcError(RpcNotEnabled);

    context.loadType = resource::kFeeMediumBurdenRpc;

    if (!context.params.isMember(jss::start))
        return rpcError(RpcInvalidParams);

    unsigned int const startIndex = context.params[jss::start].asUInt();

    if ((startIndex > 10000) && (!isUnlimited(context.role)))
        return rpcError(RpcNoPermission);

    auto trans = context.app.getRelationalDatabase().getTxHistory(startIndex);

    json::Value obj;
    json::Value& txs = obj[jss::txs];
    obj[jss::index] = startIndex;

    for (auto const& t : trans)
    {
        json::Value txJson = t->getJson(JsonOptions::Values::None);
        rpc::insertDeliverMax(txJson, t->getSTransaction()->getTxnType(), context.apiVersion);
        txs.append(txJson);
    }

    return obj;
}

}  // namespace xrpl
