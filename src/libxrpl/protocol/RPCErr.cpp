#include <xrpl/protocol/RPCErr.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/jss.h>

namespace xrpl {

struct RPCErr;

// VFALCO NOTE Deprecated function
Json::Value
rpcError(ErrorCodeI iError)
{
    Json::Value jvResult(Json::ObjectValue);
    RPC::injectError(iError, jvResult);
    return jvResult;
}

// VFALCO NOTE Deprecated function
bool
isRpcError(Json::Value jvResult)
{
    return jvResult.isObject() && jvResult.isMember(jss::error);
}

}  // namespace xrpl
