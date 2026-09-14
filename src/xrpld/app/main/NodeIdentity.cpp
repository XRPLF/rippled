#include <xrpld/app/main/NodeIdentity.h>

#include <xrpld/app/main/Application.h>
#include <xrpld/core/Config.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/config/Constants.h>
#include <xrpl/core/StartUpType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/rdb/DBInit.h>
#include <xrpl/rdb/DatabaseCon.h>
#include <xrpl/server/Wallet.h>

#include <boost/program_options/variables_map.hpp>

#include <array>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace xrpl {

namespace {

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
    // Marshal Config and the cmdline into the libxrpl-level primitives the
    // decision helpers take. Keeping the decision in libxrpl lets its tests
    // cover every branch without an xrpld Config.
    std::optional<std::string> cmdlineSeed;
    if (cmdline.contains("nodeid"))
        cmdlineSeed = cmdline["nodeid"].as<std::string>();

    std::optional<std::string> configSeedLine;
    if (config.exists(Sections::kNodeSeed))
    {
        auto const& lines = config.section(Sections::kNodeSeed).lines();
        // Present-but-empty stays as an empty string, so parseNodeIdentitySeed
        // throws the same "invalid [node_seed]" error the old code did.
        configSeedLine = lines.empty() ? std::string{} : lines.front();
    }

    auto const seed = parseNodeIdentitySeed(cmdlineSeed, configSeedLine);
    bool const newNodeId = cmdline.contains("newnodeid");

    // storedIdentity() catches its own exceptions and returns std::nullopt on
    // any read failure, so a wallet that will not open collapses into "mint".
    return selectNodeIdentity(seed, newNodeId, [&] { return storedIdentity(config, journal); });
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
