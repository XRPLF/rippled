#include <xrpld/app/main/NodeIdentity.h>

#include <xrpld/app/main/Application.h>
#include <xrpld/core/Config.h>

#include <xrpl/basics/contract.h>
#include <xrpl/config/Constants.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/rdb/DBInit.h>
#include <xrpl/rdb/DatabaseCon.h>
#include <xrpl/server/Wallet.h>

#include <boost/program_options/variables_map.hpp>

#include <array>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
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
        auto secretKey = generateSecretKey(KeyType::Secp256k1, *seed);
        auto publicKey = derivePublicKey(KeyType::Secp256k1, secretKey);

        return {publicKey, secretKey};
    }

    auto db = app.getWalletDB().checkoutDb();

    if (cmdline.contains("newnodeid"))
        clearNodeIdentity(*db);

    return getNodeIdentity(*db);
}

std::optional<std::string>
resolveNodePublicKey(
    Config const& config,
    boost::program_options::variables_map const& cmdline,
    beast::Journal journal)
{
    std::optional<Seed> seed;
    bool seedConfigured = false;

    if (cmdline.contains("nodeid"))
    {
        seedConfigured = true;
        seed = parseGenericSeed(cmdline["nodeid"].as<std::string>(), false);
    }
    else if (config.exists(Sections::kNodeSeed))
    {
        seedConfigured = true;
        if (auto const& lines = config.section(Sections::kNodeSeed).lines(); !lines.empty())
            seed = parseBase58<Seed>(lines.front());
    }

    // A configured seed decides the identity outright. A malformed or missing
    // one is reported by getNodeIdentity(), which runs later.
    if (seedConfigured)
    {
        if (!seed)
            return std::nullopt;

        auto const secretKey = generateSecretKey(KeyType::Secp256k1, *seed);
        return toBase58(TokenType::NodePublic, derivePublicKey(KeyType::Secp256k1, secretKey));
    }

    // --newnodeid discards whatever is stored.
    if (cmdline.contains("newnodeid"))
        return std::nullopt;

    try
    {
        auto setup = setupDatabaseCon(config, journal);

        // Standalone uses a temporary database, so nothing is persisted and this
        // run will mint a fresh key.
        if (setup.standAlone && setup.startUp != StartUpType::Load &&
            setup.startUp != StartUpType::LoadFile && setup.startUp != StartUpType::Replay)
        {
            return std::nullopt;
        }

        // The global pragmas include journal_mode, which rewrites the database
        // header. The wallet is opened without them everywhere else.
        setup.useGlobalPragma = false;

        // Only read an existing file: SQLite would otherwise create one.
        if (std::error_code ec; !std::filesystem::exists(setup.dataDir / kWalletDbName, ec))
        {
            return std::nullopt;
        }

        // Empty init SQL: open the existing schema, never create it.
        DatabaseCon walletDb{
            setup,
            kWalletDbName,
            std::array<std::string, 0>{},
            std::array<char const*, 0>{},
            journal};

        auto db = walletDb.checkoutDb();
        if (auto const stored = readNodeIdentity(*db))
            return toBase58(TokenType::NodePublic, stored->first);
    }
    catch (std::exception const& e)
    {
        JLOG(journal.warn()) << "Could not read the node identity: " << e.what();
    }

    return std::nullopt;
}

}  // namespace xrpl
