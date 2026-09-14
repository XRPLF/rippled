#include <xrpld/app/main/NodeIdentity.h>

#include <xrpld/app/main/Application.h>
#include <xrpld/core/Config.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/config/Constants.h>
#include <xrpl/core/StartUpType.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/tokens.h>
#include <xrpl/rdb/DBInit.h>
#include <xrpl/rdb/DatabaseCon.h>
#include <xrpl/server/Wallet.h>

#include <boost/program_options/variables_map.hpp>

#include <array>
#include <exception>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace xrpl {

namespace {

/**
 * The seed a configured `[node_seed]` or `--nodeid` names.
 *
 * @param config  The server configuration.
 * @param cmdline The command line parameters passed into the application.
 * @return The seed, or std::nullopt when neither is configured.
 * @throws std::runtime_error if the configured value is malformed.
 */
std::optional<Seed>
configuredSeed(Config const& config, boost::program_options::variables_map const& cmdline)
{
    if (cmdline.contains("nodeid"))
    {
        auto seed = parseGenericSeed(cmdline["nodeid"].as<std::string>(), false);
        if (!seed)
            Throw<std::runtime_error>("Invalid 'nodeid' in command line");
        return seed;
    }

    if (config.exists(Sections::kNodeSeed))
    {
        auto const& lines = config.section(Sections::kNodeSeed).lines();
        auto seed = lines.empty() ? std::nullopt : parseBase58<Seed>(lines.front());
        if (!seed)
        {
            Throw<std::runtime_error>(
                std::string("Invalid [") + Sections::kNodeSeed + "] in configuration file");
        }
        return seed;
    }

    return std::nullopt;
}

/**
 * The keypair a seed defines.
 *
 * @param seed The configured seed.
 * @return The derived secp256k1 keypair.
 */
std::pair<PublicKey, SecretKey>
keysFromSeed(Seed const& seed)
{
    auto const secretKey = generateSecretKey(KeyType::Secp256k1, seed);
    return {derivePublicKey(KeyType::Secp256k1, secretKey), secretKey};
}

/**
 * The stored identity, read without creating or modifying anything.
 *
 * Runs before the Application, so it opens the wallet itself rather than going
 * through getWalletDB(). Three things keep that safe: the file must already
 * exist, the init SQL is empty so the schema is never created, and the global
 * pragmas are off because they include journal_mode, which rewrites the
 * database header. The connection closes before this returns.
 *
 * @param config  The server configuration.
 * @param journal Journal for reporting an unreadable database.
 * @return The stored keypair, or std::nullopt when there is none to read.
 */
std::optional<std::pair<PublicKey, SecretKey>>
storedIdentity(Config const& config, beast::Journal journal)
{
    try
    {
        auto setup = setupDatabaseCon(config, journal);

        // Standalone gets a private temporary database, so there is nothing
        // persisted to read and nothing setup() could read back either.
        if (setup.standAlone && setup.startUp != StartUpType::Load &&
            setup.startUp != StartUpType::LoadFile && setup.startUp != StartUpType::Replay)
        {
            return std::nullopt;
        }

        setup.useGlobalPragma = false;

        if (std::error_code ec; !std::filesystem::exists(setup.dataDir / kWalletDbName, ec))
            return std::nullopt;

        DatabaseCon walletDb{
            setup,
            kWalletDbName,
            std::array<std::string, 0>{},
            std::array<char const*, 0>{},
            journal};

        auto db = walletDb.checkoutDb();
        return readNodeIdentity(*db);
    }
    catch (std::exception const& e)
    {
        JLOG(journal.warn()) << "Could not read the node identity: " << e.what();
    }

    return std::nullopt;
}

}  // namespace

std::pair<PublicKey, SecretKey>
resolveNodeIdentity(
    Config const& config,
    boost::program_options::variables_map const& cmdline,
    beast::Journal journal)
{
    // A configured seed decides the identity outright, and nothing is stored.
    if (auto const seed = configuredSeed(config, cmdline))
        return keysFromSeed(*seed);

    // --newnodeid discards whatever is stored, so mint now; getNodeIdentity()
    // clears the old row and stores this pair.
    if (!cmdline.contains("newnodeid"))
    {
        if (auto const stored = storedIdentity(config, journal))
            return *stored;
    }

    // Nothing to read: a first boot, or a standalone run's temporary database.
    // Mint here so telemetry has an identity from construction; setup()
    // persists this pair if there is a database to hold it.
    return randomKeyPair(KeyType::Secp256k1);
}

std::pair<PublicKey, SecretKey>
getNodeIdentity(
    Application& app,
    boost::program_options::variables_map const& cmdline,
    std::pair<PublicKey, SecretKey> const& resolved)
{
    // A configured seed reaches neither the reader nor the writer.
    if (cmdline.contains("nodeid") || app.config().exists(Sections::kNodeSeed))
        return resolved;

    auto db = app.getWalletDB().checkoutDb();

    if (cmdline.contains("newnodeid"))
        clearNodeIdentity(*db);

    // What is stored wins, so a restart keeps the node's identity even if
    // another process wrote one between construction and here. Telemetry's
    // resources are already built from `resolved`, so on that one run the two
    // would disagree; it needs a restart to line up, as the configuration
    // reference records.
    if (auto const stored = readNodeIdentity(*db))
        return *stored;

    // Nothing stored, or --newnodeid just cleared it. Persist the pair
    // telemetry is already reporting, so both agree from now on.
    storeNodeIdentity(*db, resolved);
    return resolved;
}

}  // namespace xrpl
