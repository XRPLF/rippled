#include <xrpld/app/main/ValidateConfig.h>

#include <xrpld/app/main/GRPCServer.h>
#include <xrpld/app/misc/AmendmentTableConfig.h>
#include <xrpld/app/misc/SHAMapStore.h>
#include <xrpld/app/misc/TxQ.h>
#include <xrpld/app/misc/ValidatorKeys.h>
#include <xrpld/app/misc/ValidatorList.h>
#include <xrpld/app/misc/ValidatorSite.h>
#include <xrpld/app/misc/setup_HashRouter.h>
#include <xrpld/core/Config.h>
#include <xrpld/core/TimeKeeper.h>
#include <xrpld/overlay/Cluster.h>
#include <xrpld/overlay/Overlay.h>
#include <xrpld/overlay/make_Overlay.h>
#include <xrpld/rpc/ServerHandler.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/make_SSLContext.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/config/Constants.h>
#include <xrpl/core/PerfLog.h>
#include <xrpl/nodestore/DummyScheduler.h>
#include <xrpl/nodestore/Manager.h>
#include <xrpl/server/Manifest.h>

#include <filesystem>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>

namespace xrpl {

void
validateConfig(Config& config, std::ostream& errors)
{
    // These helpers only parse settings or construct in-memory TLS contexts.
    // In particular, do not call Application::setup() or ServerHandler::setup().
    auto server = setupServerHandler(config, errors);
    server.makeContexts();

    Logs logs{beast::Severity::Warning};
    auto const journal = logs.journal("Config");
    setupOverlay(config, journal);
    setupDatabaseCon(config, journal);
    setupTxQ(config);
    setupHashRouter(config);
    perf::setupPerfLog(config.section(Sections::kPerf), config.configDir);
    validateAmendmentConfig(
        config.section(Sections::kAmendments), config.section(Sections::kVetoAmendments));

    GRPCServerConfig const grpc{config, journal};
    if (!grpc.serverAddress.empty() && !grpc.createServerCredentials(journal))
        throw std::runtime_error("Invalid gRPC TLS configuration");
    if (grpc.sslCertPath && grpc.sslKeyPath)
    {
        auto const context = makeSslContextAuthed(
            *grpc.sslKeyPath, *grpc.sslCertPath, grpc.sslCertChainPath.value_or(""), "");
        if (grpc.sslClientCAPath)
            context->load_verify_file(*grpc.sslClientCAPath);
    }

    SHAMapStore::Setup const nodeStore{config};
    auto const& nodeDb = config.section(Sections::kNodeDatabase);
    auto const type = get(nodeDb, Keys::kType);
    if (!node_store::Manager::instance().find(type))
        throw std::runtime_error("Invalid [node_db] type: " + type);
    node_store::DummyScheduler scheduler;
    // Backend construction parses its options. Only open() touches the database.
    auto const backend = node_store::Manager::instance().makeBackend(nodeDb, 0, scheduler, journal);
    if (auto const path = nodeDb.get(Keys::kPath);
        path && std::filesystem::exists(*path) && !std::filesystem::is_directory(*path))
        throw std::runtime_error("[node_db] path is not a directory: " + *path);

    Cluster cluster{journal};
    if (!cluster.load(config.section(Sections::kClusterNodes)))
        throw std::runtime_error("Invalid entry in cluster configuration");

    if (config.exists(Sections::kValidationSeed) &&
        config.section(Sections::kValidationSeed).lines().empty())
        throw std::runtime_error("Empty [validation_seed] in configuration");
    ValidatorKeys const keys{config, journal};
    if (keys.configInvalid())
        throw std::runtime_error("Invalid validator keys configuration");

    ManifestCache validatorManifests{journal};
    ManifestCache publisherManifests{journal};
    if (!validatorManifests.load(
            keys.manifest, config.section(Sections::kValidatorKeyRevocation).values()))
        throw std::runtime_error("Invalid configured validator manifest");
    TimeKeeper timeKeeper;
    ValidatorList validators{validatorManifests, publisherManifests, timeKeeper, "", journal};
    if (!validators.load(
            keys.keys ? std::make_optional(keys.keys->publicKey) : std::nullopt,
            config.section(Sections::kValidators).values(),
            config.section(Sections::kValidatorListKeys).values(),
            config.validatorListThreshold))
        throw std::runtime_error("Invalid entry in validator configuration");

    ValidatorSite::validate(config.section(Sections::kValidatorListSites).values());
}

}  // namespace xrpl
