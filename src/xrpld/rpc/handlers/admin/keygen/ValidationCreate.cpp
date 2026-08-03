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

// The caller is responsible for checking that the secret, if present, is a string.
static std::optional<Seed>
validationSeed(json::Value const& params)
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
json::Value
doValidationCreate(RPC::JsonContext& context)
{
    json::Value obj(json::ValueType::Object);

    // Guard the conversion validationSeed makes: json::Value::asString() throws for
    // arrays and objects, which would surface as a generic internal error instead of
    // invalid parameters, and silently stringifies scalars, so a number or a bool
    // would be hashed into a validator key as its printed form.
    if (context.params.isMember(jss::secret) && !context.params[jss::secret].isString())
        return rpcError(RpcInvalidParams);

    auto seed = validationSeed(context.params);

    if (!seed)
        return rpcError(RpcBadSeed);

    auto const privateKey = generateSecretKey(KeyType::Secp256k1, *seed);

    obj[jss::validation_public_key] =
        toBase58(TokenType::NodePublic, derivePublicKey(KeyType::Secp256k1, privateKey));

    obj[jss::validation_private_key] = toBase58(TokenType::NodePrivate, privateKey);

    obj[jss::validation_seed] = toBase58(*seed);
    obj[jss::validation_key] = seedAs1751(*seed);

    return obj;
}

}  // namespace xrpl
