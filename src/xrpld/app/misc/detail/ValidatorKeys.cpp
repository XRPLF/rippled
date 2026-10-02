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
ValidatorKeys::ValidatorKeys(Config const& config, beast::Journal j)
{
    if (config.exists(Sections::kValidatorToken) && config.exists(Sections::kValidationSeed))
    {
        configInvalid_ = true;
        JLOG(j.fatal()) << "Cannot specify both [" << Sections::kValidationSeed << "] and ["
                        << Sections::kValidatorToken << "]";
        return;
    }

    if (config.exists(Sections::kValidatorToken))
    {
        // token is non-const so it can be moved from
        if (auto token = loadValidatorToken(config.section(Sections::kValidatorToken).lines()))
        {
            auto const pk = derivePublicKey(KeyType::Secp256k1, token->validationSecret);
            auto const m = deserializeManifest(base64Decode(token->manifest));

            if (!m || pk != m->signingKey)
            {
                configInvalid_ = true;
                JLOG(j.fatal()) << "Invalid token specified in [" << Sections::kValidatorToken
                                << "]";
            }
            else
            {
                keys.emplace(m->masterKey, pk, token->validationSecret);
                nodeID = calcNodeID(m->masterKey);
                sequence = m->sequence;
                manifest = std::move(token->manifest);
            }
        }
        else
        {
            configInvalid_ = true;
            JLOG(j.fatal()) << "Invalid token specified in [" << Sections::kValidatorToken << "]";
        }
    }
    else if (config.exists(Sections::kValidationSeed))
    {
        auto const seed =
            parseBase58<Seed>(config.section(Sections::kValidationSeed).lines().front());
        if (!seed)
        {
            configInvalid_ = true;
            JLOG(j.fatal()) << "Invalid seed specified in [" << Sections::kValidationSeed << "]";
        }
        else
        {
            SecretKey const sk = generateSecretKey(KeyType::Secp256k1, *seed);
            PublicKey const pk = derivePublicKey(KeyType::Secp256k1, sk);
            keys.emplace(pk, pk, sk);
            nodeID = calcNodeID(pk);
            sequence = 0;
        }
    }
}
}  // namespace xrpl
