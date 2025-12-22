#include <xrpld/app/main/Application.h>
#include <xrpld/app/main/NodeIdentity.h>
#include <xrpld/app/rdb/Wallet.h>
#include <xrpld/core/Config.h>
#include <xrpld/core/ConfigSections.h>
#include <xrpl/protocol/KeyType.h>

namespace xrpl {

std::pair<PublicKey, SecretKey>
getNodeIdentity(
    Application& app,
    boost::program_options::variables_map const& cmdline)
{
    std::optional<Seed> seed;

    if (cmdline.count("nodeid"))
    {
        seed = parseGenericSeed(cmdline["nodeid"].as<std::string>(), false);

        if (!seed)
            Throw<std::runtime_error>("Invalid 'nodeid' in command line");
    }
    else if (app.config().exists(SECTION_NODE_SEED))
    {
        seed = parseBase58<Seed>(
            app.config().section(SECTION_NODE_SEED).lines().front());

        if (!seed)
            Throw<std::runtime_error>("Invalid [" SECTION_NODE_SEED
                                      "] in configuration file");
    }

    if (seed)
    {
        // Read key type from config, default to secp256k1
        KeyType keyType = KeyType::secp256k1;
        if (app.config().exists(SECTION_VALIDATOR_KEY_TYPE))
        {
            auto const keyTypeStr =
                app.config().section(SECTION_VALIDATOR_KEY_TYPE).lines().front();
            auto const parsedKeyType = keyTypeFromString(keyTypeStr);
            if (parsedKeyType)
            {
                keyType = *parsedKeyType;
            }
            throw std::runtime_error(
                "Invalid key type specified in [" SECTION_VALIDATOR_KEY_TYPE
                "]: " +
                keyTypeStr);
        }

        auto secretKey = generateSecretKey(keyType, *seed);
        auto publicKey = derivePublicKey(keyType, secretKey);

        return {publicKey, secretKey};
    }

    auto db = app.getWalletDB().checkoutDb();

    if (cmdline.count("newnodeid") != 0)
        clearNodeIdentity(*db);

    return getNodeIdentity(*db);
}

}  // namespace xrpl
