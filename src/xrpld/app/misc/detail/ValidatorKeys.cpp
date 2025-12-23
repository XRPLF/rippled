#include <xrpld/app/misc/Manifest.h>
#include <xrpld/app/misc/ValidatorKeys.h>
#include <xrpld/core/Config.h>
#include <xrpld/core/ConfigSections.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base64.h>

namespace ripple {
ValidatorKeys::ValidatorKeys(Config const& config, beast::Journal j)
{
    if (config.exists(SECTION_VALIDATOR_TOKEN) &&
        config.exists(SECTION_VALIDATION_SEED))
    {
        configInvalid_ = true;
        JLOG(j.fatal()) << "Cannot specify both [" SECTION_VALIDATION_SEED
                           "] and [" SECTION_VALIDATOR_TOKEN "]";
        return;
    }

    if (config.exists(SECTION_VALIDATOR_TOKEN))
    {
        // token is non-const so it can be moved from
        if (auto token = loadValidatorToken(
                config.section(SECTION_VALIDATOR_TOKEN).lines()))
        {
            auto const pk =
                derivePublicKey(KeyType::secp256k1, token->validationSecret);
            auto const m = deserializeManifest(base64_decode(token->manifest));

            if (!m || pk != m->signingKey)
            {
                configInvalid_ = true;
                JLOG(j.fatal())
                    << "Invalid token specified in [" SECTION_VALIDATOR_TOKEN
                       "]";
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
            JLOG(j.fatal())
                << "Invalid token specified in [" SECTION_VALIDATOR_TOKEN "]";
        }
    }
    else if (config.exists(SECTION_VALIDATION_SEED))
    {
        auto const seed = parseBase58<Seed>(
            config.section(SECTION_VALIDATION_SEED).lines().front());
        if (!seed)
        {
            configInvalid_ = true;
            JLOG(j.fatal())
                << "Invalid seed specified in [" SECTION_VALIDATION_SEED "]";
        }
        else
        {
            SecretKey const sk = generateSecretKey(KeyType::secp256k1, *seed);
            PublicKey const pk = derivePublicKey(KeyType::secp256k1, sk);
            keys.emplace(pk, pk, sk);
            nodeID = calcNodeID(pk);
            sequence = 0;
        }
    }
}
}  // namespace ripple
