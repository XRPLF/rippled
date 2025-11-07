#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/ValidatorList.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

Json::Value
doUnlList(RPC::JsonContext& context)
{
    Json::Value obj(Json::objectValue);

    context.app.validators().for_each_listed(
        [&unl = obj[jss::unl]](PublicKey const& publicKey, bool trusted) {
            Json::Value node(Json::objectValue);

            node[jss::pubkey_validator] =
                toBase58(TokenType::NodePublic, publicKey);
            node[jss::trusted] = trusted;

            unl.append(std::move(node));
        });

    return obj;
}

}  // namespace ripple
