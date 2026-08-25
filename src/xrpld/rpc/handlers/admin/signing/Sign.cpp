#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/detail/TransactionSign.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Fees.h>

namespace xrpl {

// {
//   tx_json: <object>,
//   secret: <secret>
// }
json::Value
doSign(RPC::JsonContext& context)
{
    if (context.role != Role::ADMIN && !context.app.config().canSign())
    {
        return RPC::makeError(RpcNotSupported, "Signing is not supported by this server.");
    }

    context.loadType = Resource::kFeeHeavyBurdenRpc;
    NetworkOPs::FailHard const failType = NetworkOPs::doFailHard(
        context.params.isMember(jss::fail_hard) && context.params[jss::fail_hard].asBool());

    auto ret = RPC::transactionSign(
        context.params,
        context.apiVersion,
        failType,
        context.role,
        context.ledgerMaster.getValidatedLedgerAge(),
        context.app);

    ret[jss::deprecated] =
        "This command has been deprecated and will be "
        "removed in a future version of the server. Please "
        "migrate to a standalone signing tool.";

    return ret;
}

}  // namespace xrpl
