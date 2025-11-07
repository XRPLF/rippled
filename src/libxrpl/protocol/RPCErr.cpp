#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

struct RPCErr;

// VFALCO NOTE Deprecated function
Json::Value
rpcError(int iError)
{
    Json::Value jvResult(Json::objectValue);
    RPC::inject_error(iError, jvResult);
    return jvResult;
}

// VFALCO NOTE Deprecated function
bool
isRpcError(Json::Value jvResult)
{
    return jvResult.isObject() && jvResult.isMember(jss::error);
}

}  // namespace ripple
