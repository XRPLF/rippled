#include <xrpld/app/main/NodeIdentity.h>

#include <xrpld/app/main/Application.h>
#include <xrpld/core/Config.h>

#include <xrpl/basics/contract.h>
#include <xrpl/config/Constants.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/server/Wallet.h>

#include <boost/program_options/variables_map.hpp>

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace xrpl {

std::pair<PublicKey, SecretKey>
getNodeIdentity(Application& app, boost::program_options::variables_map const& cmdline)
{
    std::optional<Seed> seed;

    if (cmdline.contains("nodeid"))
    {
        seed = parseGenericSeed(cmdline["nodeid"].as<std::string>(), false);

        if (!seed)
            Throw<std::runtime_error>("Invalid 'nodeid' in command line");
    }
    else if (app.config().exists(Sections::kNodeSeed))
    {
        seed = parseBase58<Seed>(app.config().section(Sections::kNodeSeed).lines().front());

        if (!seed)
        {
            Throw<std::runtime_error>(
                std::string("Invalid [") + Sections::kNodeSeed + "] in configuration file");
        }
    }

    if (seed)
    {
        // Read key type from config, default to secp256k1
        KeyType keyType = KeyType::Secp256k1;
        if (app.config().exists(Sections::kValidatorKeyType))
        {
            auto const keyTypeStr =
                app.config().section(Sections::kValidatorKeyType).lines().front();
            auto const parsedKeyType = keyTypeFromString(keyTypeStr);
            if (!parsedKeyType)
            {
                Throw<std::runtime_error>(
                    std::string("Invalid key type specified in [") +
                    Sections::kValidatorKeyType + "]: " + keyTypeStr);
            }
            keyType = *parsedKeyType;
        }

        auto secretKey = generateSecretKey(keyType, *seed);
        auto publicKey = derivePublicKey(keyType, secretKey);

        return {publicKey, secretKey};
    }

    auto db = app.getWalletDB().checkoutDb();

    if (cmdline.contains("newnodeid"))
        clearNodeIdentity(*db);

    return getNodeIdentity(*db);
}

}  // namespace xrpl
