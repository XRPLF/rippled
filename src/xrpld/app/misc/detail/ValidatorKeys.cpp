#include <xrpld/app/misc/ValidatorKeys.h>

#include <xrpld/core/Config.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base64.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/config/Constants.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/server/Manifest.h>

#include <utility>

namespace xrpl {

std::optional<KeyType>
configuredValidatorKeyType(Config const& config)
{
    if (!config.exists(Sections::kValidatorKeyType))
        return KeyType::Secp256k1;

    auto const& lines = config.section(Sections::kValidatorKeyType).lines();
    if (lines.empty())
        return std::nullopt;

    auto const type = keyTypeFromString(lines.front());
    if (type != KeyType::Secp256k1 && type != KeyType::Dilithium)
        return std::nullopt;
    return type;
}

ValidatorKeys::ValidatorKeys(Config const& config, beast::Journal j)
{
    if (config.exists(Sections::kValidatorToken) && config.exists(Sections::kValidationSeed))
    {
        configInvalid_ = true;
        JLOG(j.fatal()) << "Cannot specify both [" << Sections::kValidationSeed << "] and ["
                        << Sections::kValidatorToken << "]";
        return;
    }

    if (!config.exists(Sections::kValidatorToken) && !config.exists(Sections::kValidationSeed))
        return;

    auto const keyType = configuredValidatorKeyType(config);
    if (!keyType)
    {
        configInvalid_ = true;
        JLOG(j.fatal()) << "Invalid key type specified in [" << Sections::kValidatorKeyType
                        << "]: it must be secp256k1 or dilithium";
        return;
    }

    if (config.exists(Sections::kValidatorToken))
    {
        // token is non-const so it can be moved from
        auto token = loadValidatorToken(config.section(Sections::kValidatorToken).lines());
        if (!token)
        {
            configInvalid_ = true;
            JLOG(j.fatal()) << "Could not load token specified in [" << Sections::kValidatorToken
                            << "]";
            return;
        }

        auto const secretSize =
            *keyType == KeyType::Dilithium ? SecretKey::kDilithiumSize : SecretKey::kSize;
        if (token->validationSecret.size() != secretSize)
        {
            configInvalid_ = true;
            JLOG(j.fatal()) << "The secret in [" << Sections::kValidatorToken << "] is not a "
                            << to_string(*keyType) << " key";
            return;
        }

        auto const pk = derivePublicKey(*keyType, token->validationSecret);
        auto const m = deserializeManifest(base64Decode(token->manifest));
        if (!m || pk != m->signingKey)
        {
            configInvalid_ = true;
            JLOG(j.fatal()) << "Invalid token specified in [" << Sections::kValidatorToken << "]";
            return;
        }

        keys.emplace(m->masterKey, pk, token->validationSecret);
        nodeID = calcNodeID(m->masterKey);
        sequence = m->sequence;
        manifest = std::move(token->manifest);
        return;
    }

    auto const seed = parseBase58<Seed>(config.section(Sections::kValidationSeed).lines().front());
    if (!seed)
    {
        configInvalid_ = true;
        JLOG(j.fatal()) << "Invalid seed specified in [" << Sections::kValidationSeed << "]";
        return;
    }

    SecretKey const sk = generateSecretKey(*keyType, *seed);
    PublicKey const pk = derivePublicKey(*keyType, sk);
    keys.emplace(pk, pk, sk);
    nodeID = calcNodeID(pk);
    sequence = 0;
}
}  // namespace xrpl
