#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/TransactionSign.h>

#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/resource/Fees.h>

namespace ripple {

// {
//   SigningAccounts <array>,
//   tx_json: <object>,
// }
Json::Value
doSubmitMultiSigned(RPC::JsonContext& context)
{
    context.loadType = Resource::feeHeavyBurdenRPC;
    auto const failHard = context.params[jss::fail_hard].asBool();
    auto const failType = NetworkOPs::doFailHard(failHard);

    return RPC::transactionSubmitMultiSigned(
        context.params,
        context.apiVersion,
        failType,
        context.role,
        context.ledgerMaster.getValidatedLedgerAge(),
        context.app,
        RPC::getProcessTxnFn(context.netOps));
}

}  // namespace ripple
