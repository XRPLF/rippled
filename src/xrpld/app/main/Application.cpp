#include <xrpld/app/main/Application.h>

#include <xrpld/app/consensus/RCLValidations.h>
#include <xrpld/app/ledger/InboundLedger.h>
#include <xrpld/app/ledger/InboundLedgers.h>
#include <xrpld/app/ledger/InboundTransactions.h>
#include <xrpld/app/ledger/LedgerCleaner.h>
#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/LedgerPersistence.h>
#include <xrpld/app/ledger/LedgerReplay.h>
#include <xrpld/app/ledger/LedgerReplayer.h>
#include <xrpld/app/ledger/LedgerToJson.h>
#include <xrpld/app/ledger/OpenLedger.h>
#include <xrpld/app/ledger/OrderBookDBImpl.h>
#include <xrpld/app/ledger/TransactionMaster.h>
#include <xrpld/app/main/BasicApp.h>
#include <xrpld/app/main/CollectorManager.h>
#include <xrpld/app/main/GRPCServer.h>
#include <xrpld/app/main/LoadManager.h>
#include <xrpld/app/main/NodeIdentity.h>
#include <xrpld/app/main/NodeStoreScheduler.h>
#include <xrpld/app/misc/SHAMapStore.h>
#include <xrpld/app/misc/TxQ.h>
#include <xrpld/app/misc/ValidatorKeys.h>
#include <xrpld/app/misc/ValidatorSite.h>
#include <xrpld/app/misc/make_NetworkOPs.h>
#include <xrpld/app/misc/setup_HashRouter.h>
#include <xrpld/app/rdb/backend/SQLiteDatabase.h>
#include <xrpld/core/Config.h>
#include <xrpld/core/NetworkIDServiceImpl.h>
#include <xrpld/overlay/Cluster.h>
#include <xrpld/overlay/PeerSet.h>
#include <xrpld/overlay/make_Overlay.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/RPCHandler.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/ServerHandler.h>
#include <xrpld/rpc/detail/PathRequestManager.h>
#include <xrpld/rpc/detail/Pathfinder.h>
#include <xrpld/shamap/NodeFamily.h>

#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/MallocTrim.h>
#include <xrpl/basics/ResolverAsio.h>
#include <xrpl/basics/ToString.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/random.h>
#include <xrpl/beast/asio/io_latency_probe.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/beast/insight/Event.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/PropertyStream.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/config/Constants.h>
#include <xrpl/core/ClosureCounter.h>
#include <xrpl/core/HashRouter.h>
#include <xrpl/core/Job.h>
#include <xrpl/core/NetworkIDService.h>
#include <xrpl/core/PeerReservationTable.h>
#include <xrpl/core/PerfLog.h>
#include <xrpl/core/StartUpType.h>
#include <xrpl/crypto/csprng.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_reader.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/AmendmentTable.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/PendingSaves.h>
#include <xrpl/nodestore/Database.h>
#include <xrpl/nodestore/DummyScheduler.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ApiVersion.h>
#include <xrpl/protocol/BuildInfo.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/rdb/DatabaseCon.h>
#include <xrpl/resource/Charge.h>
#include <xrpl/resource/Consumer.h>
#include <xrpl/resource/Fees.h>
#include <xrpl/resource/ResourceManager.h>
#include <xrpl/server/LoadFeeTrack.h>
#include <xrpl/server/NetworkOPs.h>
#include <xrpl/server/Wallet.h>
#include <xrpl/server/detail/ServerImpl.h>
#include <xrpl/shamap/FullBelowCache.h>
#include <xrpl/shamap/SHAMap.h>
#include <xrpl/shamap/SHAMapMissingNode.h>
#include <xrpl/shamap/TreeNodeCache.h>
#include <xrpl/tx/apply.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/lexical_cast/bad_lexical_cast.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/system/detail/errc.hpp>
#include <boost/system/detail/error_code.hpp>
#include <boost/system/system_error.hpp>

#include <date/date.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fstream>
#include <functional>
#include <ios>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace xrpl {

static void
fixConfigPorts(Config& config, Endpoints const& endpoints);

// VFALCO TODO Move the function definitions into the class declaration
class ApplicationImp : public Application, public BasicApp
{
private:
    class IOLatencySampler
    {
    private:
        beast::insight::Event event_;
        beast::Journal journal_;
        beast::IOLatencyProbe<std::chrono::steady_clock> probe_;
        std::atomic<std::chrono::milliseconds> lastSample_;

    public:
        IOLatencySampler(
            beast::insight::Event ev,
            beast::Journal journal,
            std::chrono::milliseconds interval,
            boost::asio::io_context& ios)
            : event_(std::move(ev)), journal_(journal), probe_(interval, ios)
        {
        }

        void
        start()
        {
            probe_.sample(std::ref(*this));
        }

        template <class Duration>
        void
        operator()(Duration const& elapsed)
        {
            using namespace std::chrono;
            auto const lastSample = ceil<milliseconds>(elapsed);

            lastSample_ = lastSample;

            if (lastSample >= 10ms)
                event_.notify(lastSample);
            if (lastSample >= 500ms)
            {
                JLOG(journal_.warn()) << "io_context latency = " << lastSample.count();
            }
        }

        [[nodiscard]] std::chrono::milliseconds
        get() const
        {
            return lastSample_.load();
        }

        void
        cancel()
        {
            probe_.cancel();
        }

        void
        cancelAsync()
        {
            probe_.cancelAsync();
        }
    };

public:
    // NOLINTBEGIN(readability-identifier-naming)
    std::unique_ptr<Config> config_;
    std::unique_ptr<Logs> logs_;
    std::unique_ptr<TimeKeeper> timeKeeper_;

    std::uint64_t const instanceCookie_;

    beast::Journal journal_;
    std::unique_ptr<perf::PerfLog> perfLog_;
    Application::MutexType masterMutex_;

    // Required by the SHAMapStore
    TransactionMaster txMaster_;

    std::unique_ptr<CollectorManager> collectorManager_;
    std::unique_ptr<JobQueue> jobQueue_;
    NodeStoreScheduler nodeStoreScheduler_;
    std::unique_ptr<SHAMapStore> shaMapStore_;
    PendingSaves pendingSaves_;
    std::optional<OpenLedger> openLedger_;

    NodeCache tempNodeCache_;
    CachedSLEs cachedSLEs_;
    std::unique_ptr<NetworkIDService> networkIDService_;
    std::optional<std::pair<PublicKey, SecretKey>> nodeIdentity_;
    ValidatorKeys const validatorKeys_;

    std::unique_ptr<Resource::Manager> resourceManager_;

    std::unique_ptr<NodeStore::Database> nodeStore_;
    NodeFamily nodeFamily_;
    std::unique_ptr<OrderBookDB> orderBookDB_;
    std::unique_ptr<PathRequestManager> pathRequestManager_;
    std::unique_ptr<LedgerMaster> ledgerMaster_;
    std::unique_ptr<LedgerCleaner> ledgerCleaner_;
    std::unique_ptr<InboundLedgers> inboundLedgers_;
    std::unique_ptr<InboundTransactions> inboundTransactions_;
    std::unique_ptr<LedgerReplayer> ledgerReplayer_;
    TaggedCache<uint256, AcceptedLedger> acceptedLedgerCache_;
    std::unique_ptr<NetworkOPs> networkOPs_;
    std::unique_ptr<Cluster> cluster_;
    std::unique_ptr<PeerReservationTable> peerReservations_;
    std::unique_ptr<ManifestCache> validatorManifests_;
    std::unique_ptr<ManifestCache> publisherManifests_;
    std::unique_ptr<ValidatorList> validators_;
    std::unique_ptr<ValidatorSite> validatorSites_;
    std::unique_ptr<ServerHandler> serverHandler_;
    std::unique_ptr<AmendmentTable> amendmentTable_;
    std::unique_ptr<LoadFeeTrack> feeTrack_;
    std::unique_ptr<HashRouter> hashRouter_;
    RCLValidations validations_;
    std::unique_ptr<LoadManager> loadManager_;
    std::unique_ptr<TxQ> txQ_;
    ClosureCounter<void, boost::system::error_code const&> waitHandlerCounter_;
    boost::asio::steady_timer sweepTimer_;
    boost::asio::steady_timer entropyTimer_;

    std::optional<SQLiteDatabase> relationalDatabase_;
    std::unique_ptr<DatabaseCon> walletDB_;
    std::unique_ptr<Overlay> overlay_;
    std::optional<uint256> trapTxID_;

    boost::asio::signal_set signals_;

    std::atomic_flag isTimeToStop;

    std::atomic<bool> checkSigs_;

    std::unique_ptr<ResolverAsio> resolver_;

    IOLatencySampler io_latency_sampler_;

    std::unique_ptr<GRPCServer> grpcServer_;
    // NOLINTEND(readability-identifier-naming)

    //--------------------------------------------------------------------------

    static std::size_t
    numberOfThreads(Config const& config)
    {
#if XRPL_SINGLE_IO_SERVICE_THREAD
        return 1;
#else

        if (config.ioWorkers > 0)
            return config.ioWorkers;

        auto const cores = std::thread::hardware_concurrency();

        // Use a single thread when running on under-provisioned systems
        // or if we are configured to use minimal resources.
        if ((cores == 1) || ((config.nodeSize == 0) && (cores == 2)))
            return 1;

        // Otherwise, prefer six threads.
        return 6;
#endif
    }

    //--------------------------------------------------------------------------

    ApplicationImp(
        std::unique_ptr<Config> config,
        std::unique_ptr<Logs> logs,
        std::unique_ptr<TimeKeeper> timeKeeper)
        : BasicApp(numberOfThreads(*config))
        , config_(std::move(config))
        , logs_(std::move(logs))
        , timeKeeper_(std::move(timeKeeper))
        , instanceCookie_(1 + randInt(cryptoPrng(), std::numeric_limits<std::uint64_t>::max() - 1))
        , journal_(logs_->journal("Application"))
        // PerfLog must be started before any other threads are launched.
        , perfLog_(
              perf::makePerfLog(
                  perf::setupPerfLog(config_->section(Sections::kPerf), config_->configDir),
                  *this,
                  logs_->journal("PerfLog"),
                  [this] { signalStop("PerfLog"); }))
        , txMaster_(*this)
        , collectorManager_(makeCollectorManager(
              config_->section(Sections::kInsight),
              logs_->journal("Collector")))
        , jobQueue_(
              std::make_unique<JobQueue>(
                  [](std::unique_ptr<Config> const& config) {
                      if (config->standalone() && !config->forceMultiThread)
                          return 1;

                      if (config->workers)
                          return config->workers;

                      auto count = static_cast<int>(std::thread::hardware_concurrency());

                      // Be more aggressive about the number of threads to use
                      // for the job queue if the server is configured as
                      // "large" or "huge" if there are enough cores.
                      if (config->nodeSize >= 4 && count >= 16)
                      {
                          count = 6 + std::min(count, 8);
                      }
                      else if (config->nodeSize >= 3 && count >= 8)
                      {
                          count = 4 + std::min(count, 6);
                      }
                      else
                      {
                          count = 2 + std::min(count, 4);
                      }

                      return count;
                  }(config_),
                  collectorManager_->group("jobq"),
                  logs_->journal("JobQueue"),
                  *logs_,
                  *perfLog_))
        , nodeStoreScheduler_(*jobQueue_)
        , shaMapStore_(makeSHAMapStore(*this, nodeStoreScheduler_, logs_->journal("SHAMapStore")))
        , tempNodeCache_(
              "NodeCache",
              16384,
              std::chrono::seconds{90},
              stopwatch(),
              logs_->journal("TaggedCache"))
        , cachedSLEs_(
              "Cached SLEs",
              0,
              std::chrono::minutes(1),
              stopwatch(),
              logs_->journal("CachedSLEs"))
        , networkIDService_(std::make_unique<NetworkIDServiceImpl>(config_->networkId))
        , validatorKeys_(*config_, journal_)
        , resourceManager_(
              Resource::makeManager(collectorManager_->collector(), logs_->journal("Resource")))
        , nodeStore_(shaMapStore_->makeNodeStore(
              config_->prefetchWorkers > 0 ? config_->prefetchWorkers : 4))
        , nodeFamily_(*this, *collectorManager_)
        , orderBookDB_(makeOrderBookDb(
              *this,
              {.pathSearchMax = config_->pathSearchMax, .standalone = config_->standalone()}))
        , pathRequestManager_(
              std::make_unique<PathRequestManager>(
                  *this,
                  logs_->journal("PathRequest"),
                  collectorManager_->collector()))
        , ledgerMaster_(
              std::make_unique<LedgerMaster>(
                  *this,
                  stopwatch(),
                  collectorManager_->collector(),
                  logs_->journal("LedgerMaster")))
        , ledgerCleaner_(makeLedgerCleaner(*this, logs_->journal("LedgerCleaner")))

        // VFALCO NOTE must come before NetworkOPs to prevent a crash due
        //             to dependencies in the destructor.
        //
        , inboundLedgers_(makeInboundLedgers(*this, stopwatch(), collectorManager_->collector()))
        , inboundTransactions_(makeInboundTransactions(
              *this,
              collectorManager_->collector(),
              [this](std::shared_ptr<SHAMap> const& set, bool fromAcquire) {
                  gotTXSet(set, fromAcquire);
              }))
        , ledgerReplayer_(
              std::make_unique<LedgerReplayer>(*this, *inboundLedgers_, makePeerSetBuilder(*this)))
        , acceptedLedgerCache_(
              "AcceptedLedger",
              4,
              std::chrono::minutes{1},
              stopwatch(),
              logs_->journal("TaggedCache"))
        , networkOPs_(makeNetworkOPs(
              *this,
              stopwatch(),
              config_->standalone(),
              config_->networkQuorum,
              config_->startValid,
              *jobQueue_,
              *ledgerMaster_,
              validatorKeys_,
              getIoContext(),
              logs_->journal("NetworkOPs"),
              collectorManager_->collector()))
        , cluster_(std::make_unique<Cluster>(logs_->journal("Overlay")))
        , peerReservations_(
              std::make_unique<PeerReservationTable>(logs_->journal("PeerReservationTable")))
        , validatorManifests_(std::make_unique<ManifestCache>(logs_->journal("ManifestCache")))
        , publisherManifests_(std::make_unique<ManifestCache>(logs_->journal("ManifestCache")))
        , validators_(
              std::make_unique<ValidatorList>(
                  *validatorManifests_,
                  *publisherManifests_,
                  *timeKeeper_,
                  config_->legacy("database_path"),
                  logs_->journal("ValidatorList"),
                  config_->validationQuorum))
        , validatorSites_(std::make_unique<ValidatorSite>(*this))
        , serverHandler_(makeServerHandler(
              *this,
              getIoContext(),
              *jobQueue_,
              *networkOPs_,
              *resourceManager_,
              *collectorManager_))
        , feeTrack_(std::make_unique<LoadFeeTrack>(logs_->journal("LoadManager")))
        , hashRouter_(std::make_unique<HashRouter>(setupHashRouter(*config_), stopwatch()))
        , validations_(ValidationParms(), stopwatch(), *this, logs_->journal("Validations"))
        , loadManager_(makeLoadManager(*this, logs_->journal("LoadManager")))
        , txQ_(std::make_unique<TxQ>(setupTxQ(*config_), logs_->journal("TxQ")))
        , sweepTimer_(getIoContext())
        , entropyTimer_(getIoContext())
        , signals_(getIoContext())
        , checkSigs_(true)
        , resolver_(ResolverAsio::make(getIoContext(), logs_->journal("Resolver")))
        , io_latency_sampler_(
              collectorManager_->collector()->makeEvent("ios_latency"),
              logs_->journal("Application"),
              std::chrono::milliseconds(100),
              getIoContext())
        , grpcServer_(std::make_unique<GRPCServer>(*this))
    {
        initAccountIdCache(config_->getValueFor(SizedItem::AccountIdCacheSize));

        add(resourceManager_.get());

        //
        // VFALCO - READ THIS!
        //
        //  Do not start threads, open sockets, or do any sort of "real work"
        //  inside the constructor. Put it in start instead. Or if you must,
        //  put it in setup (but everything in setup should be moved to start
        //  anyway.
        //
        //  The reason is that the unit tests require an Application object to
        //  be created. But we don't actually start all the threads, sockets,
        //  and services when running the unit tests. Therefore anything which
        //  needs to be stopped will not get stopped correctly if it is
        //  started in this constructor.
        //

        add(ledgerCleaner_.get());
    }

    //--------------------------------------------------------------------------

    bool
    setup(boost::program_options::variables_map const& cmdline) override;
    void
    start(bool withTimers) override;
    void
    run() override;
    void
    signalStop(std::string const& msg) override;
    bool
    checkSigs() const override;
    void
    checkSigs(bool) override;
    bool
    isStopping() const override;
    int
    fdRequired() const override;

    //--------------------------------------------------------------------------

    std::uint64_t
    instanceID() const override
    {
        return instanceCookie_;
    }

    Logs&
    getLogs() override
    {
        return *logs_;
    }

    Config&
    config() override
    {
        return *config_;
    }

    CollectorManager&
    getCollectorManager() override
    {
        return *collectorManager_;
    }

    Family&
    getNodeFamily() override
    {
        return nodeFamily_;
    }

    TimeKeeper&
    getTimeKeeper() override
    {
        return *timeKeeper_;
    }

    JobQueue&
    getJobQueue() override
    {
        return *jobQueue_;
    }

    std::pair<PublicKey, SecretKey> const&
    nodeIdentity() override
    {
        if (nodeIdentity_)
            return *nodeIdentity_;

        logicError("Accessing Application::nodeIdentity() before it is initialized.");
    }

    std::optional<PublicKey const>
    getValidationPublicKey() const override
    {
        if (!validatorKeys_.keys)
            return {};

        return validatorKeys_.keys->publicKey;
    }

    NetworkOPs&
    getOPs() override
    {
        return *networkOPs_;
    }

    ServerHandler&
    getServerHandler() override
    {
        XRPL_ASSERT(
            serverHandler_,
            "xrpl::ApplicationImp::getServerHandler : non-null server "
            "handle");
        return *serverHandler_;
    }

    boost::asio::io_context&
    getIOContext() override
    {
        return getIoContext();
    }

    std::chrono::milliseconds
    getIOLatency() override
    {
        return io_latency_sampler_.get();
    }

    LedgerMaster&
    getLedgerMaster() override
    {
        return *ledgerMaster_;
    }

    LedgerCleaner&
    getLedgerCleaner() override
    {
        return *ledgerCleaner_;
    }

    LedgerReplayer&
    getLedgerReplayer() override
    {
        return *ledgerReplayer_;
    }

    InboundLedgers&
    getInboundLedgers() override
    {
        return *inboundLedgers_;
    }

    InboundTransactions&
    getInboundTransactions() override
    {
        return *inboundTransactions_;
    }

    TaggedCache<uint256, AcceptedLedger>&
    getAcceptedLedgerCache() override
    {
        return acceptedLedgerCache_;
    }

    void
    gotTXSet(std::shared_ptr<SHAMap> const& set, bool fromAcquire) const
    {
        if (set)
            networkOPs_->mapComplete(set, fromAcquire);
    }

    TransactionMaster&
    getMasterTransaction() override
    {
        return txMaster_;
    }

    perf::PerfLog&
    getPerfLog() override
    {
        return *perfLog_;
    }

    NodeCache&
    getTempNodeCache() override
    {
        return tempNodeCache_;
    }

    NodeStore::Database&
    getNodeStore() override
    {
        return *nodeStore_;
    }

    Application::MutexType&
    getMasterMutex() override
    {
        return masterMutex_;
    }

    LoadManager&
    getLoadManager() override
    {
        return *loadManager_;
    }

    Resource::Manager&
    getResourceManager() override
    {
        return *resourceManager_;
    }

    OrderBookDB&
    getOrderBookDB() override
    {
        return *orderBookDB_;
    }

    PathRequestManager&
    getPathRequestManager() override
    {
        return *pathRequestManager_;
    }

    CachedSLEs&
    getCachedSLEs() override
    {
        return cachedSLEs_;
    }

    NetworkIDService&
    getNetworkIDService() override
    {
        return *networkIDService_;
    }

    AmendmentTable&
    getAmendmentTable() override
    {
        return *amendmentTable_;
    }

    LoadFeeTrack&
    getFeeTrack() override
    {
        return *feeTrack_;
    }

    HashRouter&
    getHashRouter() override
    {
        return *hashRouter_;
    }

    RCLValidations&
    getValidations() override
    {
        return validations_;
    }

    ValidatorList&
    getValidators() override
    {
        return *validators_;
    }

    ValidatorSite&
    getValidatorSites() override
    {
        return *validatorSites_;
    }

    ManifestCache&
    getValidatorManifests() override
    {
        return *validatorManifests_;
    }

    ManifestCache&
    getPublisherManifests() override
    {
        return *publisherManifests_;
    }

    Cluster&
    getCluster() override
    {
        return *cluster_;
    }

    PeerReservationTable&
    getPeerReservations() override
    {
        return *peerReservations_;
    }

    SHAMapStore&
    getSHAMapStore() override
    {
        return *shaMapStore_;
    }

    PendingSaves&
    getPendingSaves() override
    {
        return pendingSaves_;
    }

    OpenLedger&
    getOpenLedger() override
    {
        return *openLedger_;  // NOLINT(bugprone-unchecked-optional-access) emplaced during
                              // initialization before any caller
    }

    OpenLedger const&
    getOpenLedger() const override
    {
        return *openLedger_;  // NOLINT(bugprone-unchecked-optional-access) emplaced during
                              // initialization before any caller
    }

    Overlay&
    getOverlay() override
    {
        XRPL_ASSERT(overlay_, "xrpl::ApplicationImp::overlay : non-null overlay");
        return *overlay_;  // NOLINT(bugprone-unchecked-optional-access) assert above
    }

    TxQ&
    getTxQ() override
    {
        XRPL_ASSERT(txQ_, "xrpl::ApplicationImp::getTxQ : non-null transaction queue");
        return *txQ_;  // NOLINT(bugprone-unchecked-optional-access) assert above
    }

    RelationalDatabase&
    getRelationalDatabase() override
    {
        XRPL_ASSERT(
            relationalDatabase_,
            "xrpl::ApplicationImp::getRelationalDatabase : non-null relational database");
        return *relationalDatabase_;  // NOLINT(bugprone-unchecked-optional-access) assert above
    }

    DatabaseCon&
    getWalletDB() override
    {
        XRPL_ASSERT(walletDB_, "xrpl::ApplicationImp::getWalletDB : non-null wallet database");
        return *walletDB_;
    }

    bool
    serverOkay(std::string& reason) override;

    beast::Journal
    getJournal(std::string const& name) override;

    //--------------------------------------------------------------------------

    bool
    initRelationalDatabase()
    {
        XRPL_ASSERT(
            walletDB_.get() == nullptr,
            "xrpl::ApplicationImp::initRelationalDatabase : null wallet "
            "database");

        try
        {
            relationalDatabase_.emplace(setupRelationalDatabase(*this, *config_, *jobQueue_));

            // wallet database
            auto setup = setupDatabaseCon(*config_, journal_);
            setup.useGlobalPragma = false;

            walletDB_ = makeWalletDB(setup, journal_);
        }
        catch (std::exception const& e)
        {
            JLOG(journal_.fatal()) << "Failed to initialize SQL databases: " << e.what();
            return false;
        }

        return true;
    }

    bool
    initNodeStore() const
    {
        if (config_->doImport)
        {
            auto j = logs_->journal("NodeObject");
            NodeStore::DummyScheduler dummyScheduler;
            std::unique_ptr<NodeStore::Database> source =
                NodeStore::Manager::instance().makeDatabase(
                    megabytes(config_->getValueFor(SizedItem::BurstSize, std::nullopt)),
                    dummyScheduler,
                    0,
                    config_->section(Sections::kImportNodeDatabase),
                    j);

            JLOG(j.warn()) << "Starting node import from '" << source->getName() << "' to '"
                           << nodeStore_->getName() << "'.";

            using namespace std::chrono;
            auto const start = steady_clock::now();

            nodeStore_->importDatabase(*source);

            auto const elapsed = duration_cast<seconds>(steady_clock::now() - start);
            JLOG(j.warn()) << "Node import from '" << source->getName() << "' took "
                           << elapsed.count() << " seconds.";
        }

        return true;
    }

    //--------------------------------------------------------------------------
    //
    // PropertyStream
    //

    void
    onWrite(beast::PropertyStream::Map& stream) override
    {
    }

    //--------------------------------------------------------------------------

    void
    setSweepTimer()
    {
        // Only start the timer if waitHandlerCounter_ is not yet joined.
        if (auto optionalCountedHandler =
                waitHandlerCounter_.wrap([this](boost::system::error_code const& e) {
                    if (e.value() == boost::system::errc::success)
                    {
                        jobQueue_->addJob(JtSweep, "sweep", [this]() { doSweep(); });
                    }
                    // Recover as best we can if an unexpected error occurs.
                    if (e.value() != boost::system::errc::success &&
                        e.value() != boost::asio::error::operation_aborted)
                    {
                        // Try again later and hope for the best.
                        JLOG(journal_.error())
                            << "Sweep timer got error '" << e.message() << "'.  Restarting timer.";
                        setSweepTimer();
                    }
                }))
        {
            using namespace std::chrono;
            sweepTimer_.expires_after(
                seconds{config_->sweepInterval.value_or(
                    config_->getValueFor(SizedItem::SweepInterval))});
            sweepTimer_.async_wait(std::move(*optionalCountedHandler));
        }
    }

    void
    setEntropyTimer()
    {
        // Only start the timer if waitHandlerCounter_ is not yet joined.
        if (auto optionalCountedHandler =
                waitHandlerCounter_.wrap([this](boost::system::error_code const& e) {
                    if (e.value() == boost::system::errc::success)
                    {
                        cryptoPrng().mixEntropy();
                        setEntropyTimer();
                    }
                    // Recover as best we can if an unexpected error occurs.
                    if (e.value() != boost::system::errc::success &&
                        e.value() != boost::asio::error::operation_aborted)
                    {
                        // Try again later and hope for the best.
                        JLOG(journal_.error()) << "Entropy timer got error '" << e.message()
                                               << "'.  Restarting timer.";
                        setEntropyTimer();
                    }
                }))
        {
            using namespace std::chrono_literals;
            entropyTimer_.expires_after(5min);
            entropyTimer_.async_wait(std::move(*optionalCountedHandler));
        }
    }

    void
    doSweep()
    {
        XRPL_ASSERT(
            relationalDatabase_, "xrpl::ApplicationImp::doSweep : non-null relational database");
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access) assert above
        if (!config_->standalone() && !relationalDatabase_->transactionDbHasSpace(*config_))
        {
            signalStop("Out of transaction DB space");
        }

        // VFALCO NOTE Does the order of calls matter?
        // VFALCO TODO fix the dependency inversion using an observer,
        //         have listeners register for "onSweep ()" notification.

        {
            std::shared_ptr<FullBelowCache const> const fullBelowCache =
                nodeFamily_.getFullBelowCache();

            std::shared_ptr<TreeNodeCache const> const treeNodeCache =
                nodeFamily_.getTreeNodeCache();

            std::size_t const oldFullBelowSize = fullBelowCache->size();
            std::size_t const oldTreeNodeSize = treeNodeCache->size();

            nodeFamily_.sweep();

            JLOG(journal_.debug())
                << "NodeFamily::FullBelowCache sweep.  Size before: " << oldFullBelowSize
                << "; size after: " << fullBelowCache->size();

            JLOG(journal_.debug())
                << "NodeFamily::TreeNodeCache sweep.  Size before: " << oldTreeNodeSize
                << "; size after: " << treeNodeCache->size();
        }
        {
            TaggedCache<uint256, Transaction> const& masterTxCache =
                getMasterTransaction().getCache();

            std::size_t const oldMasterTxSize = masterTxCache.size();

            getMasterTransaction().sweep();

            JLOG(journal_.debug()) << "MasterTransaction sweep.  Size before: " << oldMasterTxSize
                                   << "; size after: " << masterTxCache.size();
        }
        {
            // Sweep NodeStore database cache(s), if enabled.
            getNodeStore().sweep();
        }
        {
            std::size_t const oldLedgerMasterCacheSize = getLedgerMaster().getFetchPackCacheSize();

            getLedgerMaster().sweep();

            JLOG(journal_.debug())
                << "LedgerMaster sweep.  Size before: " << oldLedgerMasterCacheSize
                << "; size after: " << getLedgerMaster().getFetchPackCacheSize();
        }
        {
            // NodeCache == TaggedCache<SHAMapHash, Blob>
            std::size_t const oldTempNodeCacheSize = getTempNodeCache().size();

            getTempNodeCache().sweep();

            JLOG(journal_.debug()) << "TempNodeCache sweep.  Size before: " << oldTempNodeCacheSize
                                   << "; size after: " << getTempNodeCache().size();
        }
        {
            std::size_t const oldCurrentCacheSize = getValidations().sizeOfCurrentCache();
            std::size_t const oldSizeSeqEnforcesSize = getValidations().sizeOfSeqEnforcersCache();
            std::size_t const oldByLedgerSize = getValidations().sizeOfByLedgerCache();
            std::size_t const oldBySequenceSize = getValidations().sizeOfBySequenceCache();

            getValidations().expire(journal_);

            JLOG(journal_.debug())
                << "Validations Current expire.  Size before: " << oldCurrentCacheSize
                << "; size after: " << getValidations().sizeOfCurrentCache();

            JLOG(journal_.debug())
                << "Validations SeqEnforcer expire.  Size before: " << oldSizeSeqEnforcesSize
                << "; size after: " << getValidations().sizeOfSeqEnforcersCache();

            JLOG(journal_.debug())
                << "Validations ByLedger expire.  Size before: " << oldByLedgerSize
                << "; size after: " << getValidations().sizeOfByLedgerCache();

            JLOG(journal_.debug())
                << "Validations BySequence expire.  Size before: " << oldBySequenceSize
                << "; size after: " << getValidations().sizeOfBySequenceCache();
        }
        {
            std::size_t const oldInboundLedgersSize = getInboundLedgers().cacheSize();

            getInboundLedgers().sweep();

            JLOG(journal_.debug())
                << "InboundLedgers sweep.  Size before: " << oldInboundLedgersSize
                << "; size after: " << getInboundLedgers().cacheSize();
        }
        {
            size_t const oldTasksSize = getLedgerReplayer().tasksSize();
            size_t const oldDeltasSize = getLedgerReplayer().deltasSize();
            size_t const oldSkipListsSize = getLedgerReplayer().skipListsSize();

            getLedgerReplayer().sweep();

            JLOG(journal_.debug()) << "LedgerReplayer tasks sweep.  Size before: " << oldTasksSize
                                   << "; size after: " << getLedgerReplayer().tasksSize();

            JLOG(journal_.debug()) << "LedgerReplayer deltas sweep.  Size before: " << oldDeltasSize
                                   << "; size after: " << getLedgerReplayer().deltasSize();

            JLOG(journal_.debug())
                << "LedgerReplayer skipLists sweep.  Size before: " << oldSkipListsSize
                << "; size after: " << getLedgerReplayer().skipListsSize();
        }
        {
            std::size_t const oldAcceptedLedgerSize = acceptedLedgerCache_.size();

            acceptedLedgerCache_.sweep();

            JLOG(journal_.debug())
                << "AcceptedLedgerCache sweep.  Size before: " << oldAcceptedLedgerSize
                << "; size after: " << acceptedLedgerCache_.size();
        }
        {
            std::size_t const oldCachedSLEsSize = cachedSLEs_.size();

            cachedSLEs_.sweep();

            JLOG(journal_.debug()) << "CachedSLEs sweep.  Size before: " << oldCachedSLEsSize
                                   << "; size after: " << cachedSLEs_.size();
        }

        mallocTrim("doSweep", journal_);

        // Set timer to do another sweep later.
        setSweepTimer();
    }

    LedgerIndex
    getMaxDisallowedLedger() override
    {
        return maxDisallowedLedger_;
    }

    std::optional<uint256> const&
    getTrapTxID() const override
    {
        return trapTxID_;
    }

    size_t
    getNumberOfThreads() const override
    {
        return BasicApp::getNumberOfThreads();
    }

private:
    // For a newly-started validator, this is the greatest persisted ledger
    // and new validations must be greater than this.
    std::atomic<LedgerIndex> maxDisallowedLedger_{0};

    void
    startGenesisLedger();

    std::shared_ptr<Ledger>
    getLastFullLedger();

    std::shared_ptr<Ledger>
    loadLedgerFromFile(std::string const& ledgerID);

    bool
    loadOldLedger(
        std::string const& ledgerID,
        bool replay,
        bool isFilename,
        std::optional<uint256> trapTxID);

    void
    setMaxDisallowedLedger();

    Application&
    getApp() override
    {
        return *this;
    }
};

//------------------------------------------------------------------------------

// TODO Break this up into smaller, more digestible initialization segments.
bool
ApplicationImp::setup(boost::program_options::variables_map const& cmdline)
{
    // We want to intercept CTRL-C and the standard termination signal SIGTERM
    // and terminate the process. This handler will NEVER be invoked twice.
    //
    // Note that async_wait is "one-shot": for each call, the handler will be
    // invoked exactly once, either when one of the registered signals in the
    // signal set occurs or the signal set is cancelled. Subsequent signals are
    // effectively ignored (technically, they are queued up, waiting for a call
    // to async_wait).
    signals_.add(SIGINT);
    signals_.add(SIGTERM);
    signals_.async_wait([this](boost::system::error_code const& ec, int signum) {
        // Indicates the signal handler has been aborted; do nothing
        if (ec == boost::asio::error::operation_aborted)
            return;

        JLOG(journal_.info()) << "Received signal " << signum;

        if (signum == SIGTERM || signum == SIGINT)
            signalStop("Signal: " + to_string(signum));
    });

    auto debugLog = config_->getDebugLogFile();

    if (!debugLog.empty())
    {
        // Let debug messages go to the file but only WARNING or higher to
        // regular output (unless verbose)

        if (!logs_->open(debugLog))
            std::cerr << "Can't open log file " << debugLog << '\n';

        using beast::Severity;
        if (logs_->threshold() > Severity::Debug)
            logs_->threshold(Severity::Debug);
    }

    JLOG(journal_.info()) << "Process starting: " << BuildInfo::getFullVersionString()
                          << ", Instance Cookie: " << instanceCookie_;

    if (numberOfThreads(*config_) < 2)
    {
        JLOG(journal_.warn()) << "Limited to a single I/O service thread by "
                                 "system configuration.";
    }

    // Optionally turn off logging to console.
    logs_->silent(config_->silent());

    if (!initRelationalDatabase() || !initNodeStore())
        return false;

    if (!peerReservations_->load(getWalletDB()))
    {
        JLOG(journal_.fatal()) << "Cannot find peer reservations!";
        return false;
    }

    if (validatorKeys_.keys)
        setMaxDisallowedLedger();

    // Configure the amendments the server supports
    {
        auto const supported = []() {
            auto const& amendments = detail::supportedAmendments();
            std::vector<AmendmentTable::FeatureInfo> supported;
            supported.reserve(amendments.size());
            for (auto const& [a, vote] : amendments)
            {
                auto const f = xrpl::getRegisteredFeature(a);
                XRPL_ASSERT(f, "xrpl::ApplicationImp::setup : registered feature");
                if (f)
                    supported.emplace_back(a, *f, vote);
            }
            return supported;
        }();
        Section const& downVoted = config_->section(Sections::kVetoAmendments);

        Section const& upVoted = config_->section(Sections::kAmendments);

        amendmentTable_ = makeAmendmentTable(
            *this,
            config().amendmentMajorityTime,
            supported,
            upVoted,
            downVoted,
            logs_->journal("Amendments"));
    }

    Pathfinder::initPathTable();

    auto const startUp = config_->startUp;
    JLOG(journal_.debug()) << "startUp: " << startUp;
    if (startUp == StartUpType::Fresh)
    {
        JLOG(journal_.info()) << "Starting new Ledger";

        startGenesisLedger();
    }
    else if (
        startUp == StartUpType::Load || startUp == StartUpType::LoadFile ||
        startUp == StartUpType::Replay)
    {
        JLOG(journal_.info()) << "Loading specified Ledger";

        if (!loadOldLedger(
                config_->startLedger,
                startUp == StartUpType::Replay,
                startUp == StartUpType::LoadFile,
                config_->trapTxHash))
        {
            JLOG(journal_.error()) << "The specified ledger could not be loaded.";
            if (config_->fastLoad)
            {
                // Fall back to syncing from the network, such as
                // when there's no existing data.
                startGenesisLedger();
            }
            else
            {
                return false;
            }
        }
    }
    else if (startUp == StartUpType::Network)
    {
        // This should probably become the default once we have a stable
        // network.
        if (!config_->standalone())
            networkOPs_->setNeedNetworkLedger();

        startGenesisLedger();
    }
    else
    {
        startGenesisLedger();
    }

    if (auto const& forcedRange = config().forcedLedgerRangePresent)
    {
        ledgerMaster_->setLedgerRangePresent(forcedRange->first, forcedRange->second);
    }

    orderBookDB_->setup(getLedgerMaster().getCurrentLedger());

    nodeIdentity_ = getNodeIdentity(*this, cmdline);

    if (!cluster_->load(config().section(Sections::kClusterNodes)))
    {
        JLOG(journal_.fatal()) << "Invalid entry in cluster configuration.";
        return false;
    }

    {
        if (validatorKeys_.configInvalid())
            return false;

        if (!validatorManifests_->load(
                getWalletDB(),
                "ValidatorManifests",
                validatorKeys_.manifest,
                config().section(Sections::kValidatorKeyRevocation).values()))
        {
            JLOG(journal_.fatal()) << "Invalid configured validator manifest.";
            return false;
        }

        publisherManifests_->load(getWalletDB(), "PublisherManifests");

        // It is possible to have a valid ValidatorKeys object without
        // setting the signingKey or masterKey. This occurs if the
        // configuration file does not have either
        // Sections::kValidatorToken or Sections::kValidationSeed section.

        // masterKey for the configuration-file specified validator keys
        std::optional<PublicKey> localSigningKey;
        if (validatorKeys_.keys)
            localSigningKey = validatorKeys_.keys->publicKey;

        // Setup trusted validators
        if (!validators_->load(
                localSigningKey,
                config().section(Sections::kValidators).values(),
                config().section(Sections::kValidatorListKeys).values(),
                config().validatorListThreshold))
        {
            JLOG(journal_.fatal()) << "Invalid entry in validator configuration.";
            return false;
        }
    }

    if (!validatorSites_->load(config().section(Sections::kValidatorListSites).values()))
    {
        JLOG(journal_.fatal()) << "Invalid entry in [" << Sections::kValidatorListSites << "]";
        return false;
    }

    // Tell the AmendmentTable who the trusted validators are.
    amendmentTable_->trustChanged(validators_->getQuorumKeys().second);

    //----------------------------------------------------------------------
    //
    // Server
    //
    //----------------------------------------------------------------------

    // VFALCO NOTE Unfortunately, in stand-alone mode some code still
    //             foolishly calls overlay(). When this is fixed we can
    //             move the instantiation inside a conditional:
    //
    //             if (!config_.standalone())
    overlay_ = makeOverlay(
        *this,
        setupOverlay(*config_, journal_),
        *serverHandler_,
        *resourceManager_,
        *resolver_,
        getIoContext(),
        *config_,
        collectorManager_->collector());
    add(*overlay_);  // add to PropertyStream

    // start first consensus round
    if (!networkOPs_->beginConsensus(ledgerMaster_->getClosedLedger()->header().hash, {}))
    {
        JLOG(journal_.fatal()) << "Unable to start consensus";
        return false;
    }

    {
        try
        {
            auto logStream = beast::logstream{journal_.error()};
            auto setup = setupServerHandler(*config_, logStream);
            setup.makeContexts();
            serverHandler_->setup(setup, journal_);
            fixConfigPorts(*config_, serverHandler_->endpoints());
        }
        catch (std::exception const& e)
        {
            if (auto stream = journal_.fatal())
            {
                stream << "Unable to setup server handler";
                if (std::strlen(e.what()) > 0)
                    stream << ": " << e.what();
            }
            return false;
        }
    }

    // Begin connecting to network.
    if (!config_->standalone())
    {
        // Should this message be here, conceptually? In theory this sort
        // of message, if displayed, should be displayed from PeerFinder.
        if (config_->peerPrivate && config_->ipsFixed.empty())
        {
            JLOG(journal_.warn()) << "No outbound peer connections will be made";
        }

        // VFALCO NOTE the state timer resets the deadlock detector.
        //
        networkOPs_->setStateTimer();
    }
    else
    {
        JLOG(journal_.warn()) << "Running in standalone mode";

        networkOPs_->setStandAlone();
    }

    if (config_->canSign())
    {
        JLOG(journal_.warn()) << "*** The server is configured to allow the "
                                 "'sign' and 'sign_for'";
        JLOG(journal_.warn()) << "*** commands. These commands have security "
                                 "implications and have";
        JLOG(journal_.warn()) << "*** been deprecated. They will be removed "
                                 "in a future release of";
        JLOG(journal_.warn()) << "*** xrpld.";
        JLOG(journal_.warn()) << "*** If you do not use them to sign "
                                 "transactions please edit your";
        JLOG(journal_.warn()) << "*** configuration file and remove the [enable_signing] stanza.";
        JLOG(journal_.warn()) << "*** If you do use them to sign transactions "
                                 "please migrate to a";
        JLOG(journal_.warn()) << "*** standalone signing solution as soon as possible.";
    }

    //
    // Execute start up rpc commands.
    //
    for (auto const& cmd : config_->section(Sections::kRpcStartup).lines())
    {
        json::Reader jrReader;
        json::Value jvCommand;

        if (!jrReader.parse(cmd, jvCommand))
        {
            JLOG(journal_.fatal())
                << "Couldn't parse entry in [" << Sections::kRpcStartup << "]: '" << cmd;
        }

        if (!config_->quiet())
        {
            JLOG(journal_.fatal()) << "Startup RPC: " << jvCommand << std::endl;
        }

        Resource::Charge loadType = Resource::kFeeReferenceRpc;
        Resource::Consumer c;
        RPC::JsonContext context{
            {.j = getJournal("RPCHandler"),
             .app = *this,
             .loadType = loadType,
             .netOps = getOPs(),
             .ledgerMaster = getLedgerMaster(),
             .consumer = c,
             .role = Role::ADMIN,
             .coro = {},
             .infoSub = {},
             .apiVersion = RPC::kApiMaximumSupportedVersion},
            jvCommand};

        json::Value jvResult;
        RPC::doCommand(context, jvResult);

        if (!config_->quiet())
        {
            JLOG(journal_.fatal()) << "Result: " << jvResult << std::endl;
        }
    }

    validatorSites_->start();

    return true;
}

void
ApplicationImp::start(bool withTimers)
{
    JLOG(journal_.info()) << "Application starting. Version is " << BuildInfo::getVersionString();

    if (withTimers)
    {
        setSweepTimer();
        setEntropyTimer();
    }

    io_latency_sampler_.start();
    resolver_->start();
    loadManager_->start();
    shaMapStore_->start();
    if (overlay_)
        overlay_->start();

    if (grpcServer_->start())
        fixConfigPorts(*config_, {{Sections::kPortGrpc, grpcServer_->getEndpoint()}});

    ledgerCleaner_->start();
    perfLog_->start();
}

void
ApplicationImp::run()
{
    if (!config_->standalone())
    {
        // VFALCO NOTE This seems unnecessary. If we properly refactor the load
        //             manager then the stall detector can just always be
        //             "armed"
        //
        getLoadManager().activateStallDetector();
    }

    isTimeToStop.wait(false, std::memory_order_relaxed);

    JLOG(journal_.debug()) << "Application stopping";

    io_latency_sampler_.cancelAsync();

    // VFALCO Enormous hack, we have to force the probe to cancel
    //        before we stop the io_context queue or else it never
    //        unblocks in its destructor. The fix is to make all
    //        io_objects gracefully handle exit so that we can
    //        naturally return from io_context::run() instead of
    //        forcing a call to io_context::stop()
    io_latency_sampler_.cancel();

    resolver_->stopAsync();

    // NIKB This is a hack - we need to wait for the resolver to
    //      stop. before we stop the io_server_queue or weird
    //      things will happen.
    resolver_->stop();

    {
        try
        {
            sweepTimer_.cancel();
        }
        catch (boost::system::system_error const& e)
        {
            JLOG(journal_.error()) << "Application: sweepTimer cancel error: " << e.what();
        }

        try
        {
            entropyTimer_.cancel();
        }
        catch (boost::system::system_error const& e)
        {
            JLOG(journal_.error()) << "Application: entropyTimer cancel error: " << e.what();
        }
    }

    // Make sure that any waitHandlers pending in our timers are done
    // before we declare ourselves stopped.
    using namespace std::chrono_literals;

    waitHandlerCounter_.join("Application", 1s, journal_);

    validations_.flush();

    validatorSites_->stop();

    // TODO Store manifests in manifests.sqlite instead of wallet.db
    validatorManifests_->save(getWalletDB(), "ValidatorManifests", [this](PublicKey const& pubKey) {
        return getValidators().listed(pubKey);
    });

    publisherManifests_->save(getWalletDB(), "PublisherManifests", [this](PublicKey const& pubKey) {
        return getValidators().trustedPublisher(pubKey);
    });

    // The order of these stop calls is delicate.
    // Re-ordering them risks undefined behavior.
    loadManager_->stop();
    shaMapStore_->stop();
    jobQueue_->stop();
    if (overlay_)
        overlay_->stop();
    grpcServer_->stop();
    networkOPs_->stop();
    serverHandler_->stop();
    ledgerReplayer_->stop();
    inboundTransactions_->stop();
    inboundLedgers_->stop();
    ledgerCleaner_->stop();
    nodeStore_->stop();
    perfLog_->stop();

    JLOG(journal_.info()) << "Done.";
}

void
ApplicationImp::signalStop(std::string const& msg)
{
    if (!isTimeToStop.test_and_set(std::memory_order_acquire))
    {
        if (msg.empty())
        {
            JLOG(journal_.warn()) << "Server stopping";
        }
        else
            JLOG(journal_.warn()) << "Server stopping: " << msg;

        isTimeToStop.notify_all();
    }
}

bool
ApplicationImp::checkSigs() const
{
    return checkSigs_;
}

void
ApplicationImp::checkSigs(bool check)
{
    checkSigs_ = check;
}

bool
ApplicationImp::isStopping() const
{
    return isTimeToStop.test(std::memory_order_relaxed);
}

int
ApplicationImp::fdRequired() const
{
    // Standard handles, config file, misc I/O etc:
    int needed = 128;

    // 2x the configured peer limit for peer connections:
    if (overlay_)
        needed += 2 * overlay_->limit();

    // the number of fds needed by the backend (internally
    // doubled if online delete is enabled).
    needed += std::max(5, shaMapStore_->fdRequired());

    // One fd per incoming connection a port can accept, or
    // if no limit is set, assume it'll handle 256 clients.
    for (auto const& p : serverHandler_->setup().ports)
        needed += std::max(256, p.limit);

    // The minimum number of file descriptors we need is 1024:
    return std::max(1024, needed);
}

//------------------------------------------------------------------------------

void
ApplicationImp::startGenesisLedger()
{
    std::vector<uint256> const initialAmendments = (config_->startUp == StartUpType::Fresh)
        ? amendmentTable_->getDesired()
        : std::vector<uint256>{};

    std::shared_ptr<Ledger> const genesis = std::make_shared<Ledger>(
        kCreateGenesis,
        Rules{config_->features},
        config_->fees.toFees(),
        initialAmendments,
        nodeFamily_);
    ledgerMaster_->storeLedger(genesis);

    auto const next = std::make_shared<Ledger>(*genesis, getTimeKeeper().closeTime());
    next->updateSkipList();
    XRPL_ASSERT(
        next->header().seq < kXrpLedgerEarliestFees || next->read(keylet::fees()),
        "xrpl::ApplicationImp::startGenesisLedger : valid ledger fees");
    next->setImmutable();
    openLedger_.emplace(next, cachedSLEs_, logs_->journal("OpenLedger"));
    ledgerMaster_->storeLedger(next);
    ledgerMaster_->switchLCL(next);
}

std::shared_ptr<Ledger>
ApplicationImp::getLastFullLedger()
{
    auto j = getJournal("Ledger");

    try
    {
        auto const [ledger, seq, hash] =
            getLatestLedger(Rules{config_->features}, config_->fees.toFees(), *this);

        if (!ledger)
            return ledger;

        XRPL_ASSERT(
            ledger->header().seq < kXrpLedgerEarliestFees || ledger->read(keylet::fees()),
            "xrpl::ApplicationImp::getLastFullLedger : valid ledger fees");
        ledger->setImmutable();

        if (getLedgerMaster().haveLedger(seq))
            ledger->setValidated();

        if (ledger->header().hash == hash)
        {
            JLOG(j.trace()) << "Loaded ledger: " << hash;
            return ledger;
        }

        if (auto stream = j.error())
        {
            stream << "Failed on ledger";
            json::Value p;
            addJson(p, {*ledger, nullptr, static_cast<int>(LedgerFill::Options::Full)});
            stream << p;
        }

        return {};
    }
    catch (SHAMapMissingNode const& mn)
    {
        JLOG(j.warn()) << "Ledger in database: " << mn.what();
        return {};
    }
}

std::shared_ptr<Ledger>
ApplicationImp::loadLedgerFromFile(std::string const& name)
{
    try
    {
        std::ifstream ledgerFile(name, std::ios::in);

        if (!ledgerFile)
        {
            JLOG(journal_.fatal()) << "Unable to open file '" << name << "'";
            return nullptr;
        }

        json::Reader reader;
        json::Value jLedger;

        if (!reader.parse(ledgerFile, jLedger))
        {
            JLOG(journal_.fatal()) << "Unable to parse ledger JSON";
            return nullptr;
        }

        std::reference_wrapper<json::Value> ledger(jLedger);

        // accept a wrapped ledger
        if (ledger.get().isMember("result"))
            ledger = ledger.get()["result"];

        if (ledger.get().isMember("ledger"))
            ledger = ledger.get()["ledger"];

        std::uint32_t seq = 1;
        auto closeTime = getTimeKeeper().closeTime();
        using namespace std::chrono_literals;
        auto closeTimeResolution = 30s;
        bool closeTimeEstimated = false;
        std::uint64_t totalDrops = 0;

        if (ledger.get().isMember("accountState"))
        {
            if (ledger.get().isMember(jss::ledger_index))
            {
                seq = ledger.get()[jss::ledger_index].asUInt();
            }

            if (ledger.get().isMember("close_time"))
            {
                using tp = NetClock::time_point;
                using d = tp::duration;
                closeTime = tp{d{ledger.get()["close_time"].asUInt()}};
            }
            if (ledger.get().isMember("close_time_resolution"))
            {
                using namespace std::chrono;
                closeTimeResolution = seconds{ledger.get()["close_time_resolution"].asUInt()};
            }
            if (ledger.get().isMember("close_time_estimated"))
            {
                closeTimeEstimated = ledger.get()["close_time_estimated"].asBool();
            }
            if (ledger.get().isMember("total_coins"))
            {
                totalDrops =
                    beast::lexicalCastThrow<std::uint64_t>(ledger.get()["total_coins"].asString());
            }

            ledger = ledger.get()["accountState"];
        }

        if (!ledger.get().isArrayOrNull())
        {
            JLOG(journal_.fatal()) << "State nodes must be an array";
            return nullptr;
        }

        auto loadLedger = std::make_shared<Ledger>(
            seq, closeTime, Rules{config_->features}, config_->fees.toFees(), nodeFamily_);
        loadLedger->setTotalDrops(totalDrops);

        for (json::UInt index = 0; index < ledger.get().size(); ++index)
        {
            json::Value& entry = ledger.get()[index];

            if (!entry.isObjectOrNull())
            {
                JLOG(journal_.fatal()) << "Invalid entry in ledger";
                return nullptr;
            }

            uint256 uIndex;

            if (!uIndex.parseHex(entry[jss::index].asString()))
            {
                JLOG(journal_.fatal()) << "Invalid entry in ledger";
                return nullptr;
            }

            entry.removeMember(jss::index);

            STParsedJSONObject stp("sle", ledger.get()[index]);

            if (!stp.object || uIndex.isZero())
            {
                JLOG(journal_.fatal()) << "Invalid entry in ledger";
                return nullptr;
            }

            // VFALCO TODO This is the only place that
            //             constructor is used, try to remove it
            STLedgerEntry const sle(*stp.object, uIndex);

            if (!loadLedger->addSLE(sle))
            {
                JLOG(journal_.fatal()) << "Couldn't add serialized ledger: " << uIndex;
                return nullptr;
            }
        }

        loadLedger->stateMap().flushDirty(NodeObjectType::AccountNode);

        XRPL_ASSERT(
            loadLedger->header().seq < kXrpLedgerEarliestFees || loadLedger->read(keylet::fees()),
            "xrpl::ApplicationImp::loadLedgerFromFile : valid ledger fees");
        loadLedger->setAccepted(closeTime, closeTimeResolution, !closeTimeEstimated);

        return loadLedger;
    }
    catch (std::exception const& x)
    {
        JLOG(journal_.fatal()) << "Ledger contains invalid data: " << x.what();
        return nullptr;
    }
}

bool
ApplicationImp::loadOldLedger(
    std::string const& ledgerID,
    bool replay,
    bool isFileName,
    std::optional<uint256> trapTxID)
{
    try
    {
        std::shared_ptr<Ledger const> loadLedger, replayLedger;

        if (isFileName)
        {
            if (!ledgerID.empty())
                loadLedger = loadLedgerFromFile(ledgerID);
        }
        else if (ledgerID.length() == 64)
        {
            uint256 hash;

            if (hash.parseHex(ledgerID))
            {
                loadLedger =
                    loadByHash(hash, Rules{config_->features}, config_->fees.toFees(), *this);

                if (!loadLedger)
                {
                    // Try to build the ledger from the back end
                    auto il = std::make_shared<InboundLedger>(
                        *this,
                        hash,
                        0,
                        InboundLedger::Reason::GENERIC,
                        stopwatch(),
                        makeDummyPeerSet(*this));
                    if (il->checkLocal())
                        loadLedger = il->getLedger();
                }
            }
        }
        else if (ledgerID.empty() || boost::iequals(ledgerID, "latest"))
        {
            loadLedger = getLastFullLedger();
        }
        else
        {
            // assume by sequence
            std::uint32_t index = 0;

            if (beast::lexicalCastChecked(index, ledgerID))
            {
                loadLedger =
                    loadByIndex(index, Rules{config_->features}, config_->fees.toFees(), *this);
            }
        }

        if (!loadLedger)
            return false;

        if (replay)
        {
            // Replay a ledger close with same prior ledger and transactions

            // this ledger holds the transactions we want to replay
            replayLedger = loadLedger;

            JLOG(journal_.info()) << "Loading parent ledger";

            loadLedger = loadByHash(
                replayLedger->header().parentHash,
                Rules{config_->features},
                config_->fees.toFees(),
                *this);
            if (!loadLedger)
            {
                JLOG(journal_.info()) << "Loading parent ledger from node store";

                // Try to build the ledger from the back end
                auto il = std::make_shared<InboundLedger>(
                    *this,
                    replayLedger->header().parentHash,
                    0,
                    InboundLedger::Reason::GENERIC,
                    stopwatch(),
                    makeDummyPeerSet(*this));

                if (il->checkLocal())
                    loadLedger = il->getLedger();

                if (!loadLedger)
                {
                    // LCOV_EXCL_START
                    JLOG(journal_.fatal()) << "Replay ledger missing/damaged";
                    UNREACHABLE(
                        "xrpl::ApplicationImp::loadOldLedger : replay ledger "
                        "missing/damaged");
                    return false;
                    // LCOV_EXCL_STOP
                }
            }
        }
        using namespace std::chrono_literals;
        using namespace date;
        static constexpr NetClock::time_point kLedgerWarnTimePoint{
            sys_days{January / 1 / 2018} - sys_days{January / 1 / 2000}};
        if (loadLedger->header().closeTime < kLedgerWarnTimePoint)
        {
            JLOG(journal_.fatal()) << "\n\n***  WARNING   ***\n"
                                      "You are replaying a ledger from before "
                                   << to_string(kLedgerWarnTimePoint)
                                   << " UTC.\n"
                                      "This replay will not handle your ledger as it was "
                                      "originally "
                                      "handled.\nConsider running an earlier version of xrpld "
                                      "to "
                                      "get the older rules.\n*** CONTINUING ***\n";
        }

        JLOG(journal_.info()) << "Loading ledger " << loadLedger->header().hash
                              << " seq:" << loadLedger->header().seq;

        if (loadLedger->header().accountHash.isZero())
        {
            // LCOV_EXCL_START
            JLOG(journal_.fatal()) << "Ledger is empty.";
            UNREACHABLE("xrpl::ApplicationImp::loadOldLedger : ledger is empty");
            return false;
            // LCOV_EXCL_STOP
        }

        if (!loadLedger->walkLedger(getJournal("Ledger"), true))
        {
            // LCOV_EXCL_START
            JLOG(journal_.fatal()) << "Ledger is missing nodes.";
            UNREACHABLE(
                "xrpl::ApplicationImp::loadOldLedger : ledger is missing "
                "nodes");
            return false;
            // LCOV_EXCL_STOP
        }

        if (!loadLedger->isSensible())
        {
            // LCOV_EXCL_START
            json::Value j = getJson({*loadLedger, {}});
            j[jss::accountTreeHash] = to_string(loadLedger->header().accountHash);
            j[jss::transTreeHash] = to_string(loadLedger->header().txHash);
            JLOG(journal_.fatal()) << "Ledger is not sensible: " << j;
            UNREACHABLE(
                "xrpl::ApplicationImp::loadOldLedger : ledger is not "
                "sensible");
            return false;
            // LCOV_EXCL_STOP
        }

        ledgerMaster_->setLedgerRangePresent(loadLedger->header().seq, loadLedger->header().seq);

        ledgerMaster_->switchLCL(loadLedger);
        loadLedger->setValidated();
        ledgerMaster_->setFullLedger(loadLedger, true, false);
        openLedger_.emplace(loadLedger, cachedSLEs_, logs_->journal("OpenLedger"));

        if (replay)
        {
            // inject transaction(s) from the replayLedger into our open ledger
            // and build replay structure
            auto replayData = std::make_unique<LedgerReplay>(loadLedger, replayLedger);

            for (auto const& [_, tx] : replayData->orderedTxns())
            {
                (void)_;
                auto txID = tx->getTransactionID();
                if (trapTxID == txID)
                {
                    trapTxID_ = txID;
                    JLOG(journal_.debug()) << "Trap transaction set: " << txID;
                }

                auto s = std::make_shared<Serializer>();
                tx->add(*s);

                forceValidity(getHashRouter(), txID, Validity::SigGoodOnly);

                // emplaced during initialization before any caller
                // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
                openLedger_->modify([&txID, &s](OpenView& view, beast::Journal j) {
                    view.rawTxInsert(txID, std::move(s), nullptr);
                    return true;
                });
            }

            ledgerMaster_->takeReplay(std::move(replayData));

            if (trapTxID && !trapTxID_)
            {
                JLOG(journal_.fatal()) << "Ledger " << replayLedger->header().seq
                                       << " does not contain the transaction hash " << *trapTxID;
                return false;
            }
        }
    }
    catch (SHAMapMissingNode const& mn)
    {
        JLOG(journal_.fatal()) << "While loading specified ledger: " << mn.what();
        return false;
    }
    catch (boost::bad_lexical_cast&)
    {
        JLOG(journal_.fatal()) << "Ledger specified '" << ledgerID << "' is not valid";
        return false;
    }

    return true;
}

bool
ApplicationImp::serverOkay(std::string& reason)
{
    if (!config().elbSupport)
        return true;

    if (isStopping())
    {
        reason = "Server is shutting down";
        return false;
    }

    if (getOPs().isNeedNetworkLedger())
    {
        reason = "Not synchronized with network yet";
        return false;
    }

    if (getOPs().isAmendmentBlocked())
    {
        reason = "Server version too old";
        return false;
    }

    if (getOPs().isUNLBlocked())
    {
        reason = "No valid validator list available";
        return false;
    }

    if (getOPs().getOperatingMode() < OperatingMode::SYNCING)
    {
        reason = "Not synchronized with network";
        return false;
    }

    if (!getLedgerMaster().isCaughtUp(reason))
        return false;

    if (getFeeTrack().isLoadedLocal())
    {
        reason = "Too much load";
        return false;
    }

    return true;
}

beast::Journal
ApplicationImp::getJournal(std::string const& name)
{
    return logs_->journal(name);
}

void
ApplicationImp::setMaxDisallowedLedger()
{
    auto seq = getRelationalDatabase().getMaxLedgerSeq();
    if (seq)
        maxDisallowedLedger_ = *seq;

    JLOG(journal_.trace()) << "Max persisted ledger is " << maxDisallowedLedger_;
}

//------------------------------------------------------------------------------

Application::Application() : beast::PropertyStream::Source("app")
{
}

//------------------------------------------------------------------------------

std::unique_ptr<Application>
makeApplication(
    std::unique_ptr<Config> config,
    std::unique_ptr<Logs> logs,
    std::unique_ptr<TimeKeeper> timeKeeper)
{
    return std::make_unique<ApplicationImp>(
        std::move(config), std::move(logs), std::move(timeKeeper));
}

void
fixConfigPorts(Config& config, Endpoints const& endpoints)
{
    for (auto const& [name, ep] : endpoints)
    {
        if (!config.exists(name))
            continue;

        auto& section = config[name];
        auto const optPort = section.get(Keys::kPort);
        if (optPort)
        {
            std::uint16_t const port = beast::lexicalCast<std::uint16_t>(*optPort);
            if (port == 0u)
                section.set(Keys::kPort, std::to_string(ep.port()));
        }
    }
}

}  // namespace xrpl
