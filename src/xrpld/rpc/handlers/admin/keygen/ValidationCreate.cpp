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
validationSeed(json::Value const& params)
{
    if (!params.isMember(jss::secret))
        return randomSeed();

    return parseGenericSeed(params[jss::secret].asString());
}

// {
//   secret:   <string>  // optional
//   key_type: <string>  // optional: "secp256k1" (default) or "dilithium"
// }
//
// This command requires Role::ADMIN access because it makes
// no sense to ask an untrusted server for this.
json::Value
doValidationCreate(rpc::JsonContext& context)
{
    json::Value obj(json::ValueType::Object);

    auto seed = validationSeed(context.params);

    if (!seed)
        return rpcError(RpcBadSeed);

    // A validator key is secp256k1 unless the caller asks for dilithium; an Ed25519 key cannot
    // sign validations, and [validator_key_type] defaults to secp256k1 for the returned seed.
    KeyType keyType = KeyType::Secp256k1;
    if (context.params.isMember(jss::key_type))
    {
        auto const parsed = keyTypeFromString(context.params[jss::key_type].asString());
        if (parsed != KeyType::Secp256k1 && parsed != KeyType::Dilithium)
            return rpcError(RpcBadKeyType);
        keyType = *parsed;
    }

    auto const privateKey = generateSecretKey(keyType, *seed);

    obj[jss::validation_public_key] =
        toBase58(TokenType::NodePublic, derivePublicKey(keyType, privateKey));

    obj[jss::validation_private_key] = toBase58(TokenType::NodePrivate, privateKey);

    obj[jss::validation_seed] = toBase58(*seed);
    obj[jss::validation_key] = seedAs1751(*seed);
    obj[jss::key_type] = to_string(keyType);

    return obj;
}

}  // namespace xrpl
