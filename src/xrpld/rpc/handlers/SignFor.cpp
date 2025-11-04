#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/TransactionSign.h>

#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/resource/Fees.h>

namespace ripple {

// {
//   tx_json: <object>,
//   account: <signing account>
//   secret: <secret of signing account>
// }
Json::Value
doSignFor(RPC::JsonContext& context)
{
    if (context.role != Role::ADMIN && !context.app.config().canSign())
    {
        return RPC::make_error(
            rpcNOT_SUPPORTED, "Signing is not supported by this server.");
    }

    context.loadType = Resource::feeHeavyBurdenRPC;
    auto const failHard = context.params[jss::fail_hard].asBool();
    auto const failType = NetworkOPs::doFailHard(failHard);

    auto ret = RPC::transactionSignFor(
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

}  // namespace ripple
