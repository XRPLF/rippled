#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/TransactionSign.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Fees.h>

namespace xrpl {

// {
//   SigningAccounts <array>,
//   tx_json: <object>,
// }
json::Value
doSubmitMultiSigned(rpc::JsonContext& context)
{
    context.loadType = resource::kFeeHeavyBurdenRpc;
    auto const failType = rpc::getFailHard(context.params);
    if (!failType)
        return failType.error();

    return rpc::transactionSubmitMultiSigned(
        context.params,
        context.apiVersion,
        *failType,
        context.role,
        context.ledgerMaster.getValidatedLedgerAge(),
        context.app,
        rpc::getProcessTxnFn(context.netOps));
}

}  // namespace xrpl
