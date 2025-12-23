#include <xrpld/app/ledger/OpenLedger.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/TxQ.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/GRPCHandlers.h>

#include <xrpl/protocol/ErrorCodes.h>

namespace ripple {
Json::Value
doFee(RPC::JsonContext& context)
{
    auto result = context.app.getTxQ().doRPC(context.app);
    if (result.type() == Json::objectValue)
        return result;

    // LCOV_EXCL_START
    UNREACHABLE("ripple::doFee : invalid result type");
    RPC::inject_error(rpcINTERNAL, context.params);
    return context.params;
    // LCOV_EXCL_STOP
}

}  // namespace ripple
