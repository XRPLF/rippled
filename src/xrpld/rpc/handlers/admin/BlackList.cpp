#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/ResourceManager.h>

namespace xrpl {

json::Value
doBlackList(RPC::JsonContext& context)
{
    auto& rm = context.app.getResourceManager();
    if (context.params.isMember(jss::threshold))
    {
        // Guard the conversion: json::Value::asInt() throws for out-of-range,
        // non-numeric or non-scalar values, which would surface as a generic
        // internal error instead of invalid parameters. Only isInt() is accepted
        // because the json reader represents every value that fits in an int as
        // ValueType::Int; a UInt is by construction too large for the signed
        // threshold that getJson takes.
        auto const& jvThreshold = context.params[jss::threshold];
        if (!jvThreshold.isInt())
            return rpcError(RpcInvalidParams);

        return rm.getJson(jvThreshold.asInt());
    }

    return rm.getJson();
}

}  // namespace xrpl
