#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/TxQ.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>

namespace xrpl {
Json::Value
doFee(RPC::JsonContext& context)
{
    auto result = context.app.getTxQ().doRPC(context.app);
    if (result.type() == Json::objectValue)
        return result;

    // LCOV_EXCL_START
    UNREACHABLE("xrpl::doFee : invalid result type");
    RPC::inject_error(rpcINTERNAL, context.params);
    return context.params;
    // LCOV_EXCL_STOP
}

}  // namespace xrpl
