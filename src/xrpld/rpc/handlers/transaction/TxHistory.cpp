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

    // Guard the conversion: json::Value::asUInt() throws for negative,
    // out-of-range, non-numeric or non-scalar values, which would surface as a
    // generic internal error instead of invalid parameters.
    auto const& jvStart = context.params[jss::start];
    if (!jvStart.isUInt() && (!jvStart.isInt() || jvStart.asInt() < 0))
        return rpcError(RpcInvalidParams);

    unsigned int const startIndex = jvStart.asUInt();

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
