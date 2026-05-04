#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/ValidatorList.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/tokens.h>

#include <optional>
#include <utility>
#include <vector>

namespace xrpl {

json::Value
doUnlList(RPC::JsonContext& context)
{
    json::Value obj(json::ValueType::Object);

    context.app.getValidators().forEachListed(
        [&unl = obj[jss::unl]](PublicKey const& publicKey, bool trusted) {
            json::Value node(json::ValueType::Object);

            node[jss::pubkey_validator] = toBase58(TokenType::NodePublic, publicKey);
            node[jss::trusted] = trusted;

            unl.append(std::move(node));
        });

    return obj;
}

json::Value
doUnlSet(RPC::JsonContext& context)
{
    if (context.role != Role::ADMIN)
        return rpcError(RpcNoPermission);

    if (!context.params.isMember(jss::validators) || !context.params[jss::validators].isArray())
        return rpcError(RpcInvalidParams);

    auto const& vals = context.params[jss::validators];
    std::vector<PublicKey> keys;
    keys.reserve(vals.size());

    for (auto const& v : vals)
    {
        if (!v.isString())
            return rpcError(RpcInvalidParams);

        auto const pk = parseBase58<PublicKey>(TokenType::NodePublic, v.asString());
        if (!pk)
        {
            json::Value jvResult;
            jvResult[jss::error] = "invalidParams";
            jvResult[jss::error_message] = "Invalid validator key: " + v.asString();
            return jvResult;
        }
        keys.push_back(*pk);
    }

    std::optional<std::size_t> quorumOverride;
    if (context.params.isMember("quorum") && context.params["quorum"].isIntegral())
    {
        quorumOverride = context.params["quorum"].asUInt();
    }

    context.app.getValidators().debugSetTrusted(keys, quorumOverride);

    json::Value jvResult;
    jvResult[jss::validators] = json::Value(json::ValueType::Array);
    for (auto const& k : keys)
    {
        jvResult[jss::validators].append(toBase58(TokenType::NodePublic, k));
    }
    jvResult["quorum"] = static_cast<json::UInt>(context.app.getValidators().quorum());
    jvResult[jss::status] = "success";
    return jvResult;
}

}  // namespace xrpl
