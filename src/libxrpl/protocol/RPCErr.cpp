#include <xrpl/protocol/RPCErr.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/basics/TraceLog.h>

namespace xrpl {

struct RPCErr;

// VFALCO NOTE Deprecated function
json::Value
rpcError(ErrorCodeI iError)
{
    TRACE_FUNC();
    json::Value jvResult(json::ObjectValue);
    RPC::injectError(iError, jvResult);
    return jvResult;
}

// VFALCO NOTE Deprecated function
bool
isRpcError(json::Value jvResult)
{
    TRACE_FUNC();
    return jvResult.isObject() && jvResult.isMember(jss::error);
}

}  // namespace xrpl
