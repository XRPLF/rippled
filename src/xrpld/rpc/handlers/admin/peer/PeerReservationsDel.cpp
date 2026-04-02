#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/handlers/Handlers.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>

#include <optional>

namespace xrpl {

Json::Value
doPeerReservationsDel(RPC::JsonContext& context)
{
    auto const& params = context.params;

    // We repeat much of the parameter parsing from `doPeerReservationsAdd`.
    if (!params.isMember(jss::public_key))
        return RPC::missing_field_error(jss::public_key);
    if (!params[jss::public_key].isString())
        return RPC::expected_field_error(jss::public_key, "a string");

    std::optional<PublicKey> optPk =
        parseBase58<PublicKey>(TokenType::NodePublic, params[jss::public_key].asString());
    if (!optPk)
        return rpcError(rpcPUBLIC_MALFORMED);
    PublicKey const& nodeId = *optPk;

    auto const previous = context.app.getPeerReservations().erase(nodeId);

    Json::Value result{Json::objectValue};
    if (previous)
    {
        result[jss::previous] = previous->toJson();
    }
    return result;
}

}  // namespace xrpl
