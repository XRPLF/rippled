#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/ValidatorList.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/tokens.h>

#include <utility>

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

}  // namespace xrpl
