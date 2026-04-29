#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/handlers/Handlers.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/tokens.h>

#include <optional>

namespace xrpl {

json::Value
doPeerReservationsDel(RPC::JsonContext& context)
{
    auto const& params = context.params;

    // We repeat much of the parameter parsing from `doPeerReservationsAdd`.
    if (!params.isMember(jss::public_key))
        return RPC::missingFieldError(jss::public_key);
    if (!params[jss::public_key].isString())
        return RPC::expectedFieldError(jss::public_key, "a string");

    std::optional<PublicKey> optPk =
        parseBase58<PublicKey>(TokenType::NodePublic, params[jss::public_key].asString());
    if (!optPk)
        return rpcError(RpcPublicMalformed);
    PublicKey const& nodeId = *optPk;

    auto const previous = context.app.getPeerReservations().erase(nodeId);

    json::Value result{json::ObjectValue};
    if (previous)
    {
        result[jss::previous] = previous->toJson();
    }
    return result;
}

}  // namespace xrpl
