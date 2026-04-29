#include <xrpld/rpc/Context.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/tokens.h>

#include <optional>

namespace xrpl {

static std::optional<Seed>
validationSeed(Json::Value const& params)
{
    if (!params.isMember(jss::secret))
        return randomSeed();

    return parseGenericSeed(params[jss::secret].asString());
}

// {
//   secret: <string>   // optional
// }
//
// This command requires Role::ADMIN access because it makes
// no sense to ask an untrusted server for this.
Json::Value
doValidationCreate(RPC::JsonContext& context)
{
    Json::Value obj(Json::objectValue);

    auto seed = validationSeed(context.params);

    if (!seed)
        return rpcError(rpcBAD_SEED);

    auto const private_key = generateSecretKey(KeyType::secp256k1, *seed);

    obj[jss::validation_public_key] =
        toBase58(TokenType::NodePublic, derivePublicKey(KeyType::secp256k1, private_key));

    obj[jss::validation_private_key] = toBase58(TokenType::NodePrivate, private_key);

    obj[jss::validation_seed] = toBase58(*seed);
    obj[jss::validation_key] = seedAs1751(*seed);

    return obj;
}

}  // namespace xrpl
