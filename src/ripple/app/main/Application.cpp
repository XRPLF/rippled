//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/app/consensus/RCLValidations.h>
#include <ripple/app/ledger/InboundLedgers.h>
#include <ripple/app/ledger/InboundTransactions.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/LedgerToJson.h>
#include <ripple/app/ledger/OpenLedger.h>
#include <ripple/app/ledger/OrderBookDB.h>
#include <ripple/app/ledger/PendingSaves.h>
#include <ripple/app/ledger/TransactionMaster.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/main/BasicApp.h>
#include <ripple/app/main/DBInit.h>
#include <ripple/app/main/GRPCServer.h>
#include <ripple/app/main/LoadManager.h>
#include <ripple/app/main/NodeIdentity.h>
#include <ripple/app/main/NodeStoreScheduler.h>
#include <ripple/app/misc/AmendmentTable.h>
#include <ripple/app/misc/HashRouter.h>
#include <ripple/app/misc/LoadFeeTrack.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/misc/SHAMapStore.h>
#include <ripple/app/misc/TxQ.h>
#include <ripple/app/misc/ValidatorKeys.h>
#include <ripple/app/misc/ValidatorSite.h>
#include <ripple/app/paths/PathRequests.h>
#include <ripple/app/tx/apply.h>
#include <ripple/basics/ByteUtilities.h>
#include <ripple/basics/PerfLog.h>
#include <ripple/basics/ResolverAsio.h>
#include <ripple/basics/safe_cast.h>
#include <ripple/beast/asio/io_latency_probe.h>
#include <ripple/beast/core/LexicalCast.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/core/Stoppable.h>
#include <ripple/json/json_reader.h>
#include <ripple/nodestore/DatabaseShard.h>
#include <ripple/nodestore/DummyScheduler.h>
#include <ripple/overlay/Cluster.h>
#include <ripple/overlay/PeerReservationTable.h>
#include <ripple/overlay/make_Overlay.h>
#include <ripple/protocol/BuildInfo.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Protocol.h>
#include <ripple/protocol/STParsedJSON.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/ShardArchiveHandler.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <ripple/shamap/NodeFamily.h>
#include <ripple/shamap/ShardFamily.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/system/error_code.hpp>

#include <condition_variable>
#include <cstring>
#include <iostream>
#include <mutex>

namespace ripple {

// VFALCO TODO Move the function definitions into the class declaration
class ApplicationImp : public Application, public RootStoppable, public BasicApp
{
private:
    class io_latency_sampler
    {
    private:
        beast::insight::Event m_event;
        beast::Journal m_journal;
        beast::io_latency_probe<std::chrono::steady_clock> m_probe;
        std::atomic<std::chrono::milliseconds> lastSample_;

    public:
        io_latency_sampler(
            beast::insight::Event ev,
            beast::Journal journal,
            std::chrono::milliseconds interval,
            boost::asio::io_service& ios)
            : m_event(ev)
            , m_journal(journal)
            , m_probe(interval, ios)
            , lastSample_{}
        {
        }

        void
        start()
        {
            m_probe.sample(std::ref(*this));
        }

        template <class Duration>
        void
        operator()(Duration const& elapsed)
        {
            using namespace std::chrono;
            auto const lastSample = date::ceil<milliseconds>(elapsed);

            lastSample_ = lastSample;

            if (lastSample >= 10ms)
                m_event.notify(lastSample);
            if (lastSample >= 500ms)
            {
                JLOG(m_journal.warn())
                    << "io_service latency = " << lastSample.count();
            }
        }

        std::chrono::milliseconds
        get() const
        {
            return lastSample_.load();
        }

        void
        cancel()
        {
            m_probe.cancel();
        }

        void
        cancel_async()
        {
            m_probe.cancel_async();
        }
    };

public:
    std::unique_ptr<Config> config_;
    std::unique_ptr<Logs> logs_;
    std::unique_ptr<TimeKeeper> timeKeeper_;

    beast::Journal m_journal;
    std::unique_ptr<perf::PerfLog> perfLog_;
    Application::MutexType m_masterMutex;

    // Required by the SHAMapStore
    TransactionMaster m_txMaster;

    NodeStoreScheduler m_nodeStoreScheduler;
    std::unique_ptr<SHAMapStore> m_shaMapStore;
    PendingSaves pendingSaves_;
    AccountIDCache accountIDCache_;
    boost::optional<OpenLedger> openLedger_;

    // These are not Stoppable-derived
    NodeCache m_tempNodeCache;
    std::unique_ptr<CollectorManager> m_collectorManager;
    CachedSLEs cachedSLEs_;
    std::pair<PublicKey, SecretKey> nodeIdentity_;
    ValidatorKeys const validatorKeys_;

    std::unique_ptr<Resource::Manager> m_resourceManager;

    // These are Stoppable-related
    std::unique_ptr<JobQueue> m_jobQueue;
    std::unique_ptr<NodeStore::Database> m_nodeStore;
    NodeFamily nodeFamily_;
    std::unique_ptr<NodeStore::DatabaseShard> shardStore_;
    std::unique_ptr<ShardFamily> shardFamily_;
    std::unique_ptr<RPC::ShardArchiveHandler> shardArchiveHandler_;
    // VFALCO TODO Make OrderBookDB abstract
    OrderBookDB m_orderBookDB;
    std::unique_ptr<PathRequests> m_pathRequests;
    std::unique_ptr<LedgerMaster> m_ledgerMaster;
    std::unique_ptr<InboundLedgers> m_inboundLedgers;
    std::unique_ptr<InboundTransactions> m_inboundTransactions;
    TaggedCache<uint256, AcceptedLedger> m_acceptedLedgerCache;
    std::unique_ptr<NetworkOPs> m_networkOPs;
    std::unique_ptr<Cluster> cluster_;
    std::unique_ptr<PeerReservationTable> peerReservations_;
    std::unique_ptr<ManifestCache> validatorManifests_;
    std::unique_ptr<ManifestCache> publisherManifests_;
    std::unique_ptr<ValidatorList> validators_;
    std::unique_ptr<ValidatorSite> validatorSites_;
    std::unique_ptr<ServerHandler> serverHandler_;
    std::unique_ptr<AmendmentTable> m_amendmentTable;
    std::unique_ptr<LoadFeeTrack> mFeeTrack;
    std::unique_ptr<HashRouter> hashRouter_;
    RCLValidations mValidations;
    std::unique_ptr<LoadManager> m_loadManager;
    std::unique_ptr<TxQ> txQ_;
    ClosureCounter<void, boost::system::error_code const&> waitHandlerCounter_;
    boost::asio::steady_timer sweepTimer_;
    boost::asio::steady_timer entropyTimer_;
    bool startTimers_;

    std::unique_ptr<DatabaseCon> mTxnDB;
    std::unique_ptr<DatabaseCon> mLedgerDB;
    std::unique_ptr<DatabaseCon> mWalletDB;
    std::unique_ptr<Overlay> overlay_;
    std::vector<std::unique_ptr<Stoppable>> websocketServers_;

    boost::asio::signal_set m_signals;

    std::condition_variable cv_;
    std::mutex mut_;
    bool isTimeToStop = false;

    std::atomic<bool> checkSigs_;

    std::unique_ptr<ResolverAsio> m_resolver;

    io_latency_sampler m_io_latency_sampler;

    std::unique_ptr<GRPCServer> grpcServer_;

    //--------------------------------------------------------------------------

    static std::size_t
    numberOfThreads(Config const& config)
    {
#if RIPPLE_SINGLE_IO_SERVICE_THREAD
        return 1;
#else
        auto const cores = std::thread::hardware_concurrency();

        // Use a single thread when running on under-provisioned systems
        // or if we are configured to use minimal resources.
        if ((cores == 1) || ((config.NODE_SIZE == 0) && (cores == 2)))
            return 1;

        // Otherwise, prefer two threads.
        return 2;
#endif
    }

    //--------------------------------------------------------------------------

    ApplicationImp(
        std::unique_ptr<Config> config,
        std::unique_ptr<Logs> logs,
        std::unique_ptr<TimeKeeper> timeKeeper)
        : RootStoppable("Application")
        , BasicApp(numberOfThreads(*config))
        , config_(std::move(config))
        , logs_(std::move(logs))
        , timeKeeper_(std::move(timeKeeper))
        , m_journal(logs_->journal("Application"))

        // PerfLog must be started before any other threads are launched.
        , perfLog_(perf::make_PerfLog(
              perf::setup_PerfLog(
                  config_->section("perf"),
                  config_->CONFIG_DIR),
              *this,
              logs_->journal("PerfLog"),
              [this]() { signalStop(); }))

        , m_txMaster(*this)

        , m_nodeStoreScheduler(*this)

        , m_shaMapStore(make_SHAMapStore(
              *this,
              *this,
              m_nodeStoreScheduler,
              logs_->journal("SHAMapStore")))

        , accountIDCache_(128000)

        , m_tempNodeCache(
              "NodeCache",
              16384,
              std::chrono::seconds{90},
              stopwatch(),
              logs_->journal("TaggedCache"))

        , m_collectorManager(CollectorManager::New(
              config_->section(SECTION_INSIGHT),
              logs_->journal("Collector")))
        , cachedSLEs_(std::chrono::minutes(1), stopwatch())
        , validatorKeys_(*config_, m_journal)

        , m_resourceManager(Resource::make_Manager(
              m_collectorManager->collector(),
              logs_->journal("Resource")))

        // The JobQueue has to come pretty early since
        // almost everything is a Stoppable child of the JobQueue.
        //
        , m_jobQueue(std::make_unique<JobQueue>(
              m_collectorManager->group("jobq"),
              m_nodeStoreScheduler,
              logs_->journal("JobQueue"),
              *logs_,
              *perfLog_))

        , m_nodeStore(m_shaMapStore->makeNodeStore("NodeStore.main", 4))

        , nodeFamily_(*this, *m_collectorManager)

        // The shard store is optional and make_ShardStore can return null.
        , shardStore_(make_ShardStore(
              *this,
              *m_jobQueue,
              m_nodeStoreScheduler,
              4,
              logs_->journal("ShardStore")))

        , m_orderBookDB(*this, *m_jobQueue)

        , m_pathRequests(std::make_unique<PathRequests>(
              *this,
              logs_->journal("PathRequest"),
              m_collectorManager->collector()))

        , m_ledgerMaster(std::make_unique<LedgerMaster>(
              *this,
              stopwatch(),
              *m_jobQueue,
              m_collectorManager->collector(),
              logs_->journal("LedgerMaster")))

        // VFALCO NOTE must come before NetworkOPs to prevent a crash due
        //             to dependencies in the destructor.
        //
        , m_inboundLedgers(make_InboundLedgers(
              *this,
              stopwatch(),
              *m_jobQueue,
              m_collectorManager->collector()))

        , m_inboundTransactions(make_InboundTransactions(
              *this,
              *m_jobQueue,
              m_collectorManager->collector(),
              [this](std::shared_ptr<SHAMap> const& set, bool fromAcquire) {
                  gotTXSet(set, fromAcquire);
              }))

        , m_acceptedLedgerCache(
              "AcceptedLedger",
              4,
              std::chrono::minutes{1},
              stopwatch(),
              logs_->journal("TaggedCache"))

        , m_networkOPs(make_NetworkOPs(
              *this,
              stopwatch(),
              config_->standalone(),
              config_->NETWORK_QUORUM,
              config_->START_VALID,
              *m_jobQueue,
              *m_ledgerMaster,
              *m_jobQueue,
              validatorKeys_,
              get_io_service(),
              logs_->journal("NetworkOPs"),
              m_collectorManager->collector()))

        , cluster_(std::make_unique<Cluster>(logs_->journal("Overlay")))

        , peerReservations_(std::make_unique<PeerReservationTable>(
              logs_->journal("PeerReservationTable")))

        , validatorManifests_(
              std::make_unique<ManifestCache>(logs_->journal("ManifestCache")))

        , publisherManifests_(
              std::make_unique<ManifestCache>(logs_->journal("ManifestCache")))

        , validators_(std::make_unique<ValidatorList>(
              *validatorManifests_,
              *publisherManifests_,
              *timeKeeper_,
              config_->legacy("database_path"),
              logs_->journal("ValidatorList"),
              config_->VALIDATION_QUORUM))

        , validatorSites_(std::make_unique<ValidatorSite>(*this))

        , serverHandler_(make_ServerHandler(
              *this,
              *m_networkOPs,
              get_io_service(),
              *m_jobQueue,
              *m_networkOPs,
              *m_resourceManager,
              *m_collectorManager))

        , mFeeTrack(
              std::make_unique<LoadFeeTrack>(logs_->journal("LoadManager")))

        , hashRouter_(std::make_unique<HashRouter>(
              stopwatch(),
              HashRouter::getDefaultHoldTime(),
              HashRouter::getDefaultRecoverLimit()))

        , mValidations(
              ValidationParms(),
              stopwatch(),
              *this,
              logs_->journal("Validations"))

        , m_loadManager(
              make_LoadManager(*this, *this, logs_->journal("LoadManager")))

        , txQ_(
              std::make_unique<TxQ>(setup_TxQ(*config_), logs_->journal("TxQ")))

        , sweepTimer_(get_io_service())

        , entropyTimer_(get_io_service())

        , startTimers_(false)

        , m_signals(get_io_service())

        , checkSigs_(true)

        , m_resolver(
              ResolverAsio::New(get_io_service(), logs_->journal("Resolver")))

        , m_io_latency_sampler(
              m_collectorManager->collector()->make_event("ios_latency"),
              logs_->journal("Application"),
              std::chrono::milliseconds(100),
              get_io_service())
        , grpcServer_(std::make_unique<GRPCServer>(*this))
    {
        add(m_resourceManager.get());

        //
        // VFALCO - READ THIS!
        //
        //  Do not start threads, open sockets, or do any sort of "real work"
        //  inside the constructor. Put it in onStart instead. Or if you must,
        //  put it in setup (but everything in setup should be moved to onStart
        //  anyway.
        //
        //  The reason is that the unit tests require an Application object to
        //  be created. But we don't actually start all the threads, sockets,
        //  and services when running the unit tests. Therefore anything which
        //  needs to be stopped will not get stopped correctly if it is
        //  started in this constructor.
        //

        // VFALCO HACK
        m_nodeStoreScheduler.setJobQueue(*m_jobQueue);

        add(m_ledgerMaster->getPropertySource());
    }

    //--------------------------------------------------------------------------

    bool
    setup() override;
    void
    doStart(bool withTimers) override;
    void
    run() override;
    bool
    isShutdown() override;
    void
    signalStop() override;
    bool
    checkSigs() const override;
    void
    checkSigs(bool) override;
    int
    fdRequired() const override;

    //--------------------------------------------------------------------------

    Logs&
    logs() override
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
        return *m_collectorManager;
    }

    Family&
    getNodeFamily() override
    {
        return nodeFamily_;
    }

    // The shard store is an optional feature. If the sever is configured for
    // shards, this function will return a valid pointer, otherwise a nullptr.
    Family*
    getShardFamily() override
    {
        return shardFamily_.get();
    }

    TimeKeeper&
    timeKeeper() override
    {
        return *timeKeeper_;
    }

    JobQueue&
    getJobQueue() override
    {
        return *m_jobQueue;
    }

    std::pair<PublicKey, SecretKey> const&
    nodeIdentity() override
    {
        return nodeIdentity_;
    }

    PublicKey const&
    getValidationPublicKey() const override
    {
        return validatorKeys_.publicKey;
    }

    NetworkOPs&
    getOPs() override
    {
        return *m_networkOPs;
    }

    boost::asio::io_service&
    getIOService() override
    {
        return get_io_service();
    }

    std::chrono::milliseconds
    getIOLatency() override
    {
        return m_io_latency_sampler.get();
    }

    LedgerMaster&
    getLedgerMaster() override
    {
        return *m_ledgerMaster;
    }

    InboundLedgers&
    getInboundLedgers() override
    {
        return *m_inboundLedgers;
    }

    InboundTransactions&
    getInboundTransactions() override
    {
        return *m_inboundTransactions;
    }

    TaggedCache<uint256, AcceptedLedger>&
    getAcceptedLedgerCache() override
    {
        return m_acceptedLedgerCache;
    }

    void
    gotTXSet(std::shared_ptr<SHAMap> const& set, bool fromAcquire)
    {
        if (set)
            m_networkOPs->mapComplete(set, fromAcquire);
    }

    TransactionMaster&
    getMasterTransaction() override
    {
        return m_txMaster;
    }

    perf::PerfLog&
    getPerfLog() override
    {
        return *perfLog_;
    }

    NodeCache&
    getTempNodeCache() override
    {
        return m_tempNodeCache;
    }

    NodeStore::Database&
    getNodeStore() override
    {
        return *m_nodeStore;
    }

    // The shard store is an optional feature. If the sever is configured for
    // shards, this function will return a valid pointer, otherwise a nullptr.
    NodeStore::DatabaseShard*
    getShardStore() override
    {
        return shardStore_.get();
    }

    RPC::ShardArchiveHandler*
    getShardArchiveHandler(bool tryRecovery) override
    {
        static std::mutex handlerMutex;
        std::lock_guard lock(handlerMutex);

        // After constructing the handler, try to
        // initialize it. Log on error; set the
        // member variable on success.
        auto initAndSet =
            [this](std::unique_ptr<RPC::ShardArchiveHandler>&& handler) {
                if (!handler)
                    return false;

                if (!handler->init())
                {
                    JLOG(m_journal.error())
                        << "Failed to initialize ShardArchiveHandler.";

                    return false;
                }

                shardArchiveHandler_ = std::move(handler);
                return true;
            };

        // Need to resume based on state from a previous
        // run.
        if (tryRecovery)
        {
            if (shardArchiveHandler_ != nullptr)
            {
                JLOG(m_journal.error())
                    << "ShardArchiveHandler already created at startup.";

                return nullptr;
            }

            auto handler = RPC::ShardArchiveHandler::tryMakeRecoveryHandler(
                *this, *m_jobQueue);

            if (!initAndSet(std::move(handler)))
                return nullptr;
        }

        // Construct the ShardArchiveHandler
        if (shardArchiveHandler_ == nullptr)
        {
            auto handler = RPC::ShardArchiveHandler::makeShardArchiveHandler(
                *this, *m_jobQueue);

            if (!initAndSet(std::move(handler)))
                return nullptr;
        }

        return shardArchiveHandler_.get();
    }

    Application::MutexType&
    getMasterMutex() override
    {
        return m_masterMutex;
    }

    LoadManager&
    getLoadManager() override
    {
        return *m_loadManager;
    }

    Resource::Manager&
    getResourceManager() override
    {
        return *m_resourceManager;
    }

    OrderBookDB&
    getOrderBookDB() override
    {
        return m_orderBookDB;
    }

    PathRequests&
    getPathRequests() override
    {
        return *m_pathRequests;
    }

    CachedSLEs&
    cachedSLEs() override
    {
        return cachedSLEs_;
    }

    AmendmentTable&
    getAmendmentTable() override
    {
        return *m_amendmentTable;
    }

    LoadFeeTrack&
    getFeeTrack() override
    {
        return *mFeeTrack;
    }

    HashRouter&
    getHashRouter() override
    {
        return *hashRouter_;
    }

    RCLValidations&
    getValidations() override
    {
        return mValidations;
    }

    ValidatorList&
    validators() override
    {
        return *validators_;
    }

    ValidatorSite&
    validatorSites() override
    {
        return *validatorSites_;
    }

    ManifestCache&
    validatorManifests() override
    {
        return *validatorManifests_;
    }

    ManifestCache&
    publisherManifests() override
    {
        return *publisherManifests_;
    }

    Cluster&
    cluster() override
    {
        return *cluster_;
    }

    PeerReservationTable&
    peerReservations() override
    {
        return *peerReservations_;
    }

    SHAMapStore&
    getSHAMapStore() override
    {
        return *m_shaMapStore;
    }

    PendingSaves&
    pendingSaves() override
    {
        return pendingSaves_;
    }

    AccountIDCache const&
    accountIDCache() const override
    {
        return accountIDCache_;
    }

    OpenLedger&
    openLedger() override
    {
        return *openLedger_;
    }

    OpenLedger const&
    openLedger() const override
    {
        return *openLedger_;
    }

    Overlay&
    overlay() override
    {
        assert(overlay_);
        return *overlay_;
    }

    TxQ&
    getTxQ() override
    {
        assert(txQ_.get() != nullptr);
        return *txQ_;
    }

    DatabaseCon&
    getTxnDB() override
    {
        assert(mTxnDB.get() != nullptr);
        return *mTxnDB;
    }
    DatabaseCon&
    getLedgerDB() override
    {
        assert(mLedgerDB.get() != nullptr);
        return *mLedgerDB;
    }
    DatabaseCon&
    getWalletDB() override
    {
        assert(mWalletDB.get() != nullptr);
        return *mWalletDB;
    }

    bool
    serverOkay(std::string& reason) override;

    beast::Journal
    journal(std::string const& name) override;

    //--------------------------------------------------------------------------

    bool
    initSQLiteDBs()
    {
        assert(mTxnDB.get() == nullptr);
        assert(mLedgerDB.get() == nullptr);
        assert(mWalletDB.get() == nullptr);

        try
        {
            auto setup = setup_DatabaseCon(*config_, m_journal);

            // transaction database
            mTxnDB = std::make_unique<DatabaseCon>(
                setup,
                TxDBName,
                TxDBPragma,
                TxDBInit,
                DatabaseCon::CheckpointerSetup{m_jobQueue.get(), &logs()});
            mTxnDB->getSession() << boost::str(
                boost::format("PRAGMA cache_size=-%d;") %
                kilobytes(config_->getValueFor(SizedItem::txnDBCache)));

            if (!setup.standAlone || setup.startUp == Config::LOAD ||
                setup.startUp == Config::LOAD_FILE ||
                setup.startUp == Config::REPLAY)
            {
                // Check if AccountTransactions has primary key
                std::string cid, name, type;
                std::size_t notnull, dflt_value, pk;
                soci::indicator ind;
                soci::statement st =
                    (mTxnDB->getSession().prepare
                         << ("PRAGMA table_info(AccountTransactions);"),
                     soci::into(cid),
                     soci::into(name),
                     soci::into(type),
                     soci::into(notnull),
                     soci::into(dflt_value, ind),
                     soci::into(pk));

                st.execute();
                while (st.fetch())
                {
                    if (pk == 1)
                    {
                        JLOG(m_journal.fatal())
                            << "AccountTransactions database "
                               "should not have a primary key";
                        return false;
                    }
                }
            }

            // ledger database
            mLedgerDB = std::make_unique<DatabaseCon>(
                setup,
                LgrDBName,
                LgrDBPragma,
                LgrDBInit,
                DatabaseCon::CheckpointerSetup{m_jobQueue.get(), &logs()});
            mLedgerDB->getSession() << boost::str(
                boost::format("PRAGMA cache_size=-%d;") %
                kilobytes(config_->getValueFor(SizedItem::lgrDBCache)));

            // wallet database
            setup.useGlobalPragma = false;
            mWalletDB = std::make_unique<DatabaseCon>(
                setup,
                WalletDBName,
                std::array<char const*, 0>(),
                WalletDBInit);
        }
        catch (std::exception const& e)
        {
            JLOG(m_journal.fatal())
                << "Failed to initialize SQLite databases: " << e.what();
            return false;
        }

        return true;
    }

    bool
    initNodeStore()
    {
        if (config_->doImport)
        {
            auto j = logs_->journal("NodeObject");
            NodeStore::DummyScheduler dummyScheduler;
            RootStoppable dummyRoot{"DummyRoot"};
            std::unique_ptr<NodeStore::Database> source =
                NodeStore::Manager::instance().make_Database(
                    "NodeStore.import",
                    dummyScheduler,
                    0,
                    dummyRoot,
                    config_->section(ConfigSection::importNodeDatabase()),
                    j);

            JLOG(j.warn()) << "Starting node import from '" << source->getName()
                           << "' to '" << m_nodeStore->getName() << "'.";

            using namespace std::chrono;
            auto const start = steady_clock::now();

            m_nodeStore->import(*source);

            auto const elapsed =
                duration_cast<seconds>(steady_clock::now() - start);
            JLOG(j.warn()) << "Node import from '" << source->getName()
                           << "' took " << elapsed.count() << " seconds.";
        }

        // tune caches
        using namespace std::chrono;
        m_nodeStore->tune(
            config_->getValueFor(SizedItem::nodeCacheSize),
            seconds{config_->getValueFor(SizedItem::nodeCacheAge)});

        m_ledgerMaster->tune(
            config_->getValueFor(SizedItem::ledgerSize),
            seconds{config_->getValueFor(SizedItem::ledgerAge)});

        return true;
    }

    //--------------------------------------------------------------------------
    //
    // Stoppable
    //

    void
    onPrepare() override
    {
    }

    void
    onStart() override
    {
        JLOG(m_journal.info()) << "Application starting. Version is "
                               << BuildInfo::getVersionString();

        using namespace std::chrono_literals;
        if (startTimers_)
        {
            setSweepTimer();
            setEntropyTimer();
        }

        m_io_latency_sampler.start();

        m_resolver->start();
    }

    // Called to indicate shutdown.
    void
    onStop() override
    {
        JLOG(m_journal.debug()) << "Application stopping";

        m_io_latency_sampler.cancel_async();

        // VFALCO Enormous hack, we have to force the probe to cancel
        //        before we stop the io_service queue or else it never
        //        unblocks in its destructor. The fix is to make all
        //        io_objects gracefully handle exit so that we can
        //        naturally return from io_service::run() instead of
        //        forcing a call to io_service::stop()
        m_io_latency_sampler.cancel();

        m_resolver->stop_async();

        // NIKB This is a hack - we need to wait for the resolver to
        //      stop. before we stop the io_server_queue or weird
        //      things will happen.
        m_resolver->stop();

        {
            boost::system::error_code ec;
            sweepTimer_.cancel(ec);
            if (ec)
            {
                JLOG(m_journal.error())
                    << "Application: sweepTimer cancel error: " << ec.message();
            }

            ec.clear();
            entropyTimer_.cancel(ec);
            if (ec)
            {
                JLOG(m_journal.error())
                    << "Application: entropyTimer cancel error: "
                    << ec.message();
            }
        }
        // Make sure that any waitHandlers pending in our timers are done
        // before we declare ourselves stopped.
        using namespace std::chrono_literals;
        waitHandlerCounter_.join("Application", 1s, m_journal);

        mValidations.flush();

        validatorSites_->stop();

        // TODO Store manifests in manifests.sqlite instead of wallet.db
        validatorManifests_->save(
            getWalletDB(),
            "ValidatorManifests",
            [this](PublicKey const& pubKey) {
                return validators().listed(pubKey);
            });

        publisherManifests_->save(
            getWalletDB(),
            "PublisherManifests",
            [this](PublicKey const& pubKey) {
                return validators().trustedPublisher(pubKey);
            });

        stopped();
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
        if (auto optionalCountedHandler = waitHandlerCounter_.wrap(
                [this](boost::system::error_code const& e) {
                    if ((e.value() == boost::system::errc::success) &&
                        (!m_jobQueue->isStopped()))
                    {
                        m_jobQueue->addJob(
                            jtSWEEP, "sweep", [this](Job&) { doSweep(); });
                    }
                    // Recover as best we can if an unexpected error occurs.
                    if (e.value() != boost::system::errc::success &&
                        e.value() != boost::asio::error::operation_aborted)
                    {
                        // Try again later and hope for the best.
                        JLOG(m_journal.error())
                            << "Sweep timer got error '" << e.message()
                            << "'.  Restarting timer.";
                        setSweepTimer();
                    }
                }))
        {
            using namespace std::chrono;
            sweepTimer_.expires_from_now(
                seconds{config_->getValueFor(SizedItem::sweepInterval)});
            sweepTimer_.async_wait(std::move(*optionalCountedHandler));
        }
    }

    void
    setEntropyTimer()
    {
        // Only start the timer if waitHandlerCounter_ is not yet joined.
        if (auto optionalCountedHandler = waitHandlerCounter_.wrap(
                [this](boost::system::error_code const& e) {
                    if (e.value() == boost::system::errc::success)
                    {
                        crypto_prng().mix_entropy();
                        setEntropyTimer();
                    }
                    // Recover as best we can if an unexpected error occurs.
                    if (e.value() != boost::system::errc::success &&
                        e.value() != boost::asio::error::operation_aborted)
                    {
                        // Try again later and hope for the best.
                        JLOG(m_journal.error())
                            << "Entropy timer got error '" << e.message()
                            << "'.  Restarting timer.";
                        setEntropyTimer();
                    }
                }))
        {
            using namespace std::chrono_literals;
            entropyTimer_.expires_from_now(5min);
            entropyTimer_.async_wait(std::move(*optionalCountedHandler));
        }
    }

    void
    doSweep()
    {
        if (!config_->standalone())
        {
            boost::filesystem::space_info space =
                boost::filesystem::space(config_->legacy("database_path"));

            if (space.available < megabytes(512))
            {
                JLOG(m_journal.fatal())
                    << "Remaining free disk space is less than 512MB";
                signalStop();
            }

            DatabaseCon::Setup dbSetup = setup_DatabaseCon(*config_);
            boost::filesystem::path dbPath = dbSetup.dataDir / TxDBName;
            boost::system::error_code ec;
            boost::optional<std::uint64_t> dbSize =
                boost::filesystem::file_size(dbPath, ec);
            if (ec)
            {
                JLOG(m_journal.error())
                    << "Error checking transaction db file size: "
                    << ec.message();
                dbSize.reset();
            }

            auto db = mTxnDB->checkoutDb();
            static auto const pageSize = [&] {
                std::uint32_t ps;
                *db << "PRAGMA page_size;", soci::into(ps);
                return ps;
            }();
            static auto const maxPages = [&] {
                std::uint32_t mp;
                *db << "PRAGMA max_page_count;", soci::into(mp);
                return mp;
            }();
            std::uint32_t pageCount;
            *db << "PRAGMA page_count;", soci::into(pageCount);
            std::uint32_t freePages = maxPages - pageCount;
            std::uint64_t freeSpace =
                safe_cast<std::uint64_t>(freePages) * pageSize;
            JLOG(m_journal.info())
                << "Transaction DB pathname: " << dbPath.string()
                << "; file size: " << dbSize.value_or(-1) << " bytes"
                << "; SQLite page size: " << pageSize << " bytes"
                << "; Free pages: " << freePages
                << "; Free space: " << freeSpace << " bytes; "
                << "Note that this does not take into account available disk "
                   "space.";

            if (freeSpace < megabytes(512))
            {
                JLOG(m_journal.fatal())
                    << "Free SQLite space for transaction db is less than "
                       "512MB. To fix this, rippled must be executed with the "
                       "\"--vacuum\" parameter before restarting. "
                       "Note that this activity can take multiple days, "
                       "depending on database size.";
                signalStop();
            }
        }

        // VFALCO NOTE Does the order of calls matter?
        // VFALCO TODO fix the dependency inversion using an observer,
        //         have listeners register for "onSweep ()" notification.

        nodeFamily_.sweep();
        if (shardFamily_)
            shardFamily_->sweep();
        getMasterTransaction().sweep();
        getNodeStore().sweep();
        if (shardStore_)
            shardStore_->sweep();
        getLedgerMaster().sweep();
        getTempNodeCache().sweep();
        getValidations().expire();
        getInboundLedgers().sweep();
        m_acceptedLedgerCache.sweep();
        cachedSLEs_.expire();

        // Set timer to do another sweep later.
        setSweepTimer();
    }

    LedgerIndex
    getMaxDisallowedLedger() override
    {
        return maxDisallowedLedger_;
    }

private:
    // For a newly-started validator, this is the greatest persisted ledger
    // and new validations must be greater than this.
    std::atomic<LedgerIndex> maxDisallowedLedger_{0};

    bool
    nodeToShards();
    bool
    validateShards();
    void
    startGenesisLedger();

    std::shared_ptr<Ledger>
    getLastFullLedger();

    std::shared_ptr<Ledger>
    loadLedgerFromFile(std::string const& ledgerID);

    bool
    loadOldLedger(std::string const& ledgerID, bool replay, bool isFilename);

    void
    setMaxDisallowedLedger();
};

//------------------------------------------------------------------------------

// TODO Break this up into smaller, more digestible initialization segments.
bool
ApplicationImp::setup()
{
    // We want to intercept CTRL-C and the standard termination signal SIGTERM
    // and terminate the process. This handler will NEVER be invoked twice.
    //
    // Note that async_wait is "one-shot": for each call, the handler will be
    // invoked exactly once, either when one of the registered signals in the
    // signal set occurs or the signal set is cancelled. Subsequent signals are
    // effectively ignored (technically, they are queued up, waiting for a call
    // to async_wait).
    m_signals.add(SIGINT);
    m_signals.add(SIGTERM);
    m_signals.async_wait(
        [this](boost::system::error_code const& ec, int signum) {
            // Indicates the signal handler has been aborted; do nothing
            if (ec == boost::asio::error::operation_aborted)
                return;

            JLOG(m_journal.info()) << "Received signal " << signum;

            if (signum == SIGTERM || signum == SIGINT)
                signalStop();
        });

    assert(mTxnDB == nullptr);

    auto debug_log = config_->getDebugLogFile();

    if (!debug_log.empty())
    {
        // Let debug messages go to the file but only WARNING or higher to
        // regular output (unless verbose)

        if (!logs_->open(debug_log))
            std::cerr << "Can't open log file " << debug_log << '\n';

        using namespace beast::severities;
        if (logs_->threshold() > kDebug)
            logs_->threshold(kDebug);
    }
    JLOG(m_journal.info()) << "process starting: "
                           << BuildInfo::getFullVersionString();

    if (numberOfThreads(*config_) < 2)
    {
        JLOG(m_journal.warn()) << "Limited to a single I/O service thread by "
                                  "system configuration.";
    }

    // Optionally turn off logging to console.
    logs_->silent(config_->silent());

    m_jobQueue->setThreadCount(config_->WORKERS, config_->standalone());
    grpcServer_->run();

    if (!config_->standalone())
        timeKeeper_->run(config_->SNTP_SERVERS);

    if (!initSQLiteDBs() || !initNodeStore())
        return false;

    if (shardStore_)
    {
        shardFamily_ =
            std::make_unique<ShardFamily>(*this, *m_collectorManager);

        if (!shardStore_->init())
            return false;
    }

    if (!peerReservations_->load(getWalletDB()))
    {
        JLOG(m_journal.fatal()) << "Cannot find peer reservations!";
        return false;
    }

    if (validatorKeys_.publicKey.size())
        setMaxDisallowedLedger();

    // Configure the amendments the server supports
    {
        auto const& sa = detail::supportedAmendments();
        std::vector<std::string> saHashes;
        saHashes.reserve(sa.size());
        for (auto const& name : sa)
        {
            auto const f = getRegisteredFeature(name);
            BOOST_ASSERT(f);
            if (f)
                saHashes.push_back(to_string(*f) + " " + name);
        }
        Section supportedAmendments("Supported Amendments");
        supportedAmendments.append(saHashes);

        Section enabledAmendments = config_->section(SECTION_AMENDMENTS);

        m_amendmentTable = make_AmendmentTable(
            config().AMENDMENT_MAJORITY_TIME,
            supportedAmendments,
            enabledAmendments,
            config_->section(SECTION_VETO_AMENDMENTS),
            logs_->journal("Amendments"));
    }

    Pathfinder::initPathTable();

    auto const startUp = config_->START_UP;
    if (startUp == Config::FRESH)
    {
        JLOG(m_journal.info()) << "Starting new Ledger";

        startGenesisLedger();
    }
    else if (
        startUp == Config::LOAD || startUp == Config::LOAD_FILE ||
        startUp == Config::REPLAY)
    {
        JLOG(m_journal.info()) << "Loading specified Ledger";

        if (!loadOldLedger(
                config_->START_LEDGER,
                startUp == Config::REPLAY,
                startUp == Config::LOAD_FILE))
        {
            JLOG(m_journal.error())
                << "The specified ledger could not be loaded.";
            return false;
        }
    }
    else if (startUp == Config::NETWORK)
    {
        // This should probably become the default once we have a stable
        // network.
        if (!config_->standalone())
            m_networkOPs->setNeedNetworkLedger();

        startGenesisLedger();
    }
    else
    {
        startGenesisLedger();
    }

    m_orderBookDB.setup(getLedgerMaster().getCurrentLedger());

    nodeIdentity_ = loadNodeIdentity(*this);

    if (!cluster_->load(config().section(SECTION_CLUSTER_NODES)))
    {
        JLOG(m_journal.fatal()) << "Invalid entry in cluster configuration.";
        return false;
    }

    {
        if (validatorKeys_.configInvalid())
            return false;

        if (!validatorManifests_->load(
                getWalletDB(),
                "ValidatorManifests",
                validatorKeys_.manifest,
                config().section(SECTION_VALIDATOR_KEY_REVOCATION).values()))
        {
            JLOG(m_journal.fatal()) << "Invalid configured validator manifest.";
            return false;
        }

        publisherManifests_->load(getWalletDB(), "PublisherManifests");

        // Setup trusted validators
        if (!validators_->load(
                validatorKeys_.publicKey,
                config().section(SECTION_VALIDATORS).values(),
                config().section(SECTION_VALIDATOR_LIST_KEYS).values()))
        {
            JLOG(m_journal.fatal())
                << "Invalid entry in validator configuration.";
            return false;
        }
    }

    if (!validatorSites_->load(
            config().section(SECTION_VALIDATOR_LIST_SITES).values()))
    {
        JLOG(m_journal.fatal())
            << "Invalid entry in [" << SECTION_VALIDATOR_LIST_SITES << "]";
        return false;
    }

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
    overlay_ = make_Overlay(
        *this,
        setup_Overlay(*config_),
        *m_jobQueue,
        *serverHandler_,
        *m_resourceManager,
        *m_resolver,
        get_io_service(),
        *config_,
        m_collectorManager->collector());
    add(*overlay_);  // add to PropertyStream

    if (!config_->standalone())
    {
        // validation and node import require the sqlite db
        if (config_->nodeToShard && !nodeToShards())
            return false;

        if (config_->validateShards && !validateShards())
            return false;
    }

    validatorSites_->start();

    // start first consensus round
    if (!m_networkOPs->beginConsensus(
            m_ledgerMaster->getClosedLedger()->info().hash))
    {
        JLOG(m_journal.fatal()) << "Unable to start consensus";
        return false;
    }

    {
        try
        {
            auto setup = setup_ServerHandler(
                *config_, beast::logstream{m_journal.error()});
            setup.makeContexts();
            serverHandler_->setup(setup, m_journal);
        }
        catch (std::exception const& e)
        {
            if (auto stream = m_journal.fatal())
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
        if (config_->PEER_PRIVATE && config_->IPS_FIXED.empty())
        {
            JLOG(m_journal.warn())
                << "No outbound peer connections will be made";
        }

        // VFALCO NOTE the state timer resets the deadlock detector.
        //
        m_networkOPs->setStateTimer();
    }
    else
    {
        JLOG(m_journal.warn()) << "Running in standalone mode";

        m_networkOPs->setStandAlone();
    }

    if (config_->canSign())
    {
        JLOG(m_journal.warn()) << "*** The server is configured to allow the "
                                  "'sign' and 'sign_for'";
        JLOG(m_journal.warn()) << "*** commands. These commands have security "
                                  "implications and have";
        JLOG(m_journal.warn()) << "*** been deprecated. They will be removed "
                                  "in a future release of";
        JLOG(m_journal.warn()) << "*** rippled.";
        JLOG(m_journal.warn()) << "*** If you do not use them to sign "
                                  "transactions please edit your";
        JLOG(m_journal.warn())
            << "*** configuration file and remove the [enable_signing] stanza.";
        JLOG(m_journal.warn()) << "*** If you do use them to sign transactions "
                                  "please migrate to a";
        JLOG(m_journal.warn())
            << "*** standalone signing solution as soon as possible.";
    }

    //
    // Execute start up rpc commands.
    //
    for (auto cmd : config_->section(SECTION_RPC_STARTUP).lines())
    {
        Json::Reader jrReader;
        Json::Value jvCommand;

        if (!jrReader.parse(cmd, jvCommand))
        {
            JLOG(m_journal.fatal()) << "Couldn't parse entry in ["
                                    << SECTION_RPC_STARTUP << "]: '" << cmd;
        }

        if (!config_->quiet())
        {
            JLOG(m_journal.fatal())
                << "Startup RPC: " << jvCommand << std::endl;
        }

        Resource::Charge loadType = Resource::feeReferenceRPC;
        Resource::Consumer c;
        RPC::JsonContext context{
            {journal("RPCHandler"),
             *this,
             loadType,
             getOPs(),
             getLedgerMaster(),
             c,
             Role::ADMIN,
             {},
             {},
             RPC::ApiMaximumSupportedVersion},
            jvCommand};

        Json::Value jvResult;
        RPC::doCommand(context, jvResult);

        if (!config_->quiet())
        {
            JLOG(m_journal.fatal()) << "Result: " << jvResult << std::endl;
        }
    }

    if (shardStore_)
    {
        try
        {
            // Create a ShardArchiveHandler if recovery
            // is needed (there's a state database left
            // over from a previous run).
            auto handler = getShardArchiveHandler(true);

            // Recovery is needed.
            if (handler)
            {
                if (!handler->start())
                {
                    JLOG(m_journal.fatal())
                        << "Failed to start ShardArchiveHandler.";

                    return false;
                }
            }
        }
        catch (std::exception const& e)
        {
            JLOG(m_journal.fatal())
                << "Exception when starting ShardArchiveHandler from "
                   "state database: "
                << e.what();

            return false;
        }
    }

    return true;
}

void
ApplicationImp::doStart(bool withTimers)
{
    startTimers_ = withTimers;
    prepare();
    start();
}

void
ApplicationImp::run()
{
    if (!config_->standalone())
    {
        // VFALCO NOTE This seems unnecessary. If we properly refactor the load
        //             manager then the deadlock detector can just always be
        //             "armed"
        //
        getLoadManager().activateDeadlockDetector();
    }

    {
        std::unique_lock<std::mutex> lk{mut_};
        cv_.wait(lk, [this] { return isTimeToStop; });
    }

    // Stop the server. When this returns, all
    // Stoppable objects should be stopped.
    JLOG(m_journal.info()) << "Received shutdown request";
    stop(m_journal);
    JLOG(m_journal.info()) << "Done.";
}

void
ApplicationImp::signalStop()
{
    // Unblock the main thread (which is sitting in run()).
    // When we get C++20 this can use std::latch.
    std::lock_guard lk{mut_};

    if (!isTimeToStop)
    {
        isTimeToStop = true;
        cv_.notify_all();
    }
}

bool
ApplicationImp::isShutdown()
{
    // from Stoppable mixin
    return isStopped();
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

int
ApplicationImp::fdRequired() const
{
    // Standard handles, config file, misc I/O etc:
    int needed = 128;

    // 2x the configured peer limit for peer connections:
    needed += 2 * overlay_->limit();

    // the number of fds needed by the backend (internally
    // doubled if online delete is enabled).
    needed += std::max(5, m_shaMapStore->fdRequired());

    if (shardStore_)
        needed += shardStore_->fdRequired();

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
    std::vector<uint256> initialAmendments =
        (config_->START_UP == Config::FRESH) ? m_amendmentTable->getDesired()
                                             : std::vector<uint256>{};

    std::shared_ptr<Ledger> const genesis = std::make_shared<Ledger>(
        create_genesis, *config_, initialAmendments, nodeFamily_);
    m_ledgerMaster->storeLedger(genesis);

    auto const next =
        std::make_shared<Ledger>(*genesis, timeKeeper().closeTime());
    next->updateSkipList();
    next->setImmutable(*config_);
    openLedger_.emplace(next, cachedSLEs_, logs_->journal("OpenLedger"));
    m_ledgerMaster->storeLedger(next);
    m_ledgerMaster->switchLCL(next);
}

std::shared_ptr<Ledger>
ApplicationImp::getLastFullLedger()
{
    auto j = journal("Ledger");

    try
    {
        auto const [ledger, seq, hash] =
            loadLedgerHelper("order by LedgerSeq desc limit 1", *this);

        if (!ledger)
            return ledger;

        ledger->setImmutable(*config_);

        if (getLedgerMaster().haveLedger(seq))
            ledger->setValidated();

        if (ledger->info().hash == hash)
        {
            JLOG(j.trace()) << "Loaded ledger: " << hash;
            return ledger;
        }

        if (auto stream = j.error())
        {
            stream << "Failed on ledger";
            Json::Value p;
            addJson(p, {*ledger, LedgerFill::full});
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
            JLOG(m_journal.fatal()) << "Unable to open file '" << name << "'";
            return nullptr;
        }

        Json::Reader reader;
        Json::Value jLedger;

        if (!reader.parse(ledgerFile, jLedger))
        {
            JLOG(m_journal.fatal()) << "Unable to parse ledger JSON";
            return nullptr;
        }

        std::reference_wrapper<Json::Value> ledger(jLedger);

        // accept a wrapped ledger
        if (ledger.get().isMember("result"))
            ledger = ledger.get()["result"];

        if (ledger.get().isMember("ledger"))
            ledger = ledger.get()["ledger"];

        std::uint32_t seq = 1;
        auto closeTime = timeKeeper().closeTime();
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
                closeTimeResolution =
                    seconds{ledger.get()["close_time_resolution"].asUInt()};
            }
            if (ledger.get().isMember("close_time_estimated"))
            {
                closeTimeEstimated =
                    ledger.get()["close_time_estimated"].asBool();
            }
            if (ledger.get().isMember("total_coins"))
            {
                totalDrops = beast::lexicalCastThrow<std::uint64_t>(
                    ledger.get()["total_coins"].asString());
            }

            ledger = ledger.get()["accountState"];
        }

        if (!ledger.get().isArrayOrNull())
        {
            JLOG(m_journal.fatal()) << "State nodes must be an array";
            return nullptr;
        }

        auto loadLedger =
            std::make_shared<Ledger>(seq, closeTime, *config_, nodeFamily_);
        loadLedger->setTotalDrops(totalDrops);

        for (Json::UInt index = 0; index < ledger.get().size(); ++index)
        {
            Json::Value& entry = ledger.get()[index];

            if (!entry.isObjectOrNull())
            {
                JLOG(m_journal.fatal()) << "Invalid entry in ledger";
                return nullptr;
            }

            uint256 uIndex;

            if (!uIndex.SetHex(entry[jss::index].asString()))
            {
                JLOG(m_journal.fatal()) << "Invalid entry in ledger";
                return nullptr;
            }

            entry.removeMember(jss::index);

            STParsedJSONObject stp("sle", ledger.get()[index]);

            if (!stp.object || uIndex.isZero())
            {
                JLOG(m_journal.fatal()) << "Invalid entry in ledger";
                return nullptr;
            }

            // VFALCO TODO This is the only place that
            //             constructor is used, try to remove it
            STLedgerEntry sle(*stp.object, uIndex);

            if (!loadLedger->addSLE(sle))
            {
                JLOG(m_journal.fatal())
                    << "Couldn't add serialized ledger: " << uIndex;
                return nullptr;
            }
        }

        loadLedger->stateMap().flushDirty(
            hotACCOUNT_NODE, loadLedger->info().seq);

        loadLedger->setAccepted(
            closeTime, closeTimeResolution, !closeTimeEstimated, *config_);

        return loadLedger;
    }
    catch (std::exception const& x)
    {
        JLOG(m_journal.fatal()) << "Ledger contains invalid data: " << x.what();
        return nullptr;
    }
}

bool
ApplicationImp::loadOldLedger(
    std::string const& ledgerID,
    bool replay,
    bool isFileName)
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

            if (hash.SetHex(ledgerID))
            {
                loadLedger = loadByHash(hash, *this);

                if (!loadLedger)
                {
                    // Try to build the ledger from the back end
                    auto il = std::make_shared<InboundLedger>(
                        *this,
                        hash,
                        0,
                        InboundLedger::Reason::GENERIC,
                        stopwatch());
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
            std::uint32_t index;

            if (beast::lexicalCastChecked(index, ledgerID))
                loadLedger = loadByIndex(index, *this);
        }

        if (!loadLedger)
            return false;

        if (replay)
        {
            // Replay a ledger close with same prior ledger and transactions

            // this ledger holds the transactions we want to replay
            replayLedger = loadLedger;

            JLOG(m_journal.info()) << "Loading parent ledger";

            loadLedger = loadByHash(replayLedger->info().parentHash, *this);
            if (!loadLedger)
            {
                JLOG(m_journal.info())
                    << "Loading parent ledger from node store";

                // Try to build the ledger from the back end
                auto il = std::make_shared<InboundLedger>(
                    *this,
                    replayLedger->info().parentHash,
                    0,
                    InboundLedger::Reason::GENERIC,
                    stopwatch());

                if (il->checkLocal())
                    loadLedger = il->getLedger();

                if (!loadLedger)
                {
                    JLOG(m_journal.fatal()) << "Replay ledger missing/damaged";
                    assert(false);
                    return false;
                }
            }
        }
        using namespace std::chrono_literals;
        using namespace date;
        static constexpr NetClock::time_point ledgerWarnTimePoint{
            sys_days{January / 1 / 2018} - sys_days{January / 1 / 2000}};
        if (loadLedger->info().closeTime < ledgerWarnTimePoint)
        {
            JLOG(m_journal.fatal())
                << "\n\n***  WARNING   ***\n"
                   "You are replaying a ledger from before "
                << to_string(ledgerWarnTimePoint)
                << " UTC.\n"
                   "This replay will not handle your ledger as it was "
                   "originally "
                   "handled.\nConsider running an earlier version of rippled "
                   "to "
                   "get the older rules.\n*** CONTINUING ***\n";
        }

        JLOG(m_journal.info()) << "Loading ledger " << loadLedger->info().hash
                               << " seq:" << loadLedger->info().seq;

        if (loadLedger->info().accountHash.isZero())
        {
            JLOG(m_journal.fatal()) << "Ledger is empty.";
            assert(false);
            return false;
        }

        if (!loadLedger->walkLedger(journal("Ledger")))
        {
            JLOG(m_journal.fatal()) << "Ledger is missing nodes.";
            assert(false);
            return false;
        }

        if (!loadLedger->assertSane(journal("Ledger")))
        {
            JLOG(m_journal.fatal()) << "Ledger is not sane.";
            assert(false);
            return false;
        }

        m_ledgerMaster->setLedgerRangePresent(
            loadLedger->info().seq, loadLedger->info().seq);

        m_ledgerMaster->switchLCL(loadLedger);
        loadLedger->setValidated();
        m_ledgerMaster->setFullLedger(loadLedger, true, false);
        openLedger_.emplace(
            loadLedger, cachedSLEs_, logs_->journal("OpenLedger"));

        if (replay)
        {
            // inject transaction(s) from the replayLedger into our open ledger
            // and build replay structure
            auto replayData =
                std::make_unique<LedgerReplay>(loadLedger, replayLedger);

            for (auto const& [_, tx] : replayData->orderedTxns())
            {
                (void)_;
                auto txID = tx->getTransactionID();

                auto s = std::make_shared<Serializer>();
                tx->add(*s);

                forceValidity(getHashRouter(), txID, Validity::SigGoodOnly);

                openLedger_->modify(
                    [&txID, &s](OpenView& view, beast::Journal j) {
                        view.rawTxInsert(txID, std::move(s), nullptr);
                        return true;
                    });
            }

            m_ledgerMaster->takeReplay(std::move(replayData));
        }
    }
    catch (SHAMapMissingNode const& mn)
    {
        JLOG(m_journal.fatal())
            << "While loading specified ledger: " << mn.what();
        return false;
    }
    catch (boost::bad_lexical_cast&)
    {
        JLOG(m_journal.fatal())
            << "Ledger specified '" << ledgerID << "' is not valid";
        return false;
    }

    return true;
}

bool
ApplicationImp::serverOkay(std::string& reason)
{
    if (!config().ELB_SUPPORT)
        return true;

    if (isShutdown())
    {
        reason = "Server is shutting down";
        return false;
    }

    if (getOPs().isNeedNetworkLedger())
    {
        reason = "Not synchronized with network yet";
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

    if (getOPs().isAmendmentBlocked())
    {
        reason = "Server version too old";
        return false;
    }

    return true;
}

beast::Journal
ApplicationImp::journal(std::string const& name)
{
    return logs_->journal(name);
}

bool
ApplicationImp::nodeToShards()
{
    assert(overlay_);
    assert(!config_->standalone());

    if (config_->section(ConfigSection::shardDatabase()).empty())
    {
        JLOG(m_journal.fatal())
            << "The [shard_db] configuration setting must be set";
        return false;
    }
    if (!shardStore_)
    {
        JLOG(m_journal.fatal()) << "Invalid [shard_db] configuration";
        return false;
    }
    shardStore_->import(getNodeStore());
    return true;
}

bool
ApplicationImp::validateShards()
{
    assert(overlay_);
    assert(!config_->standalone());

    if (config_->section(ConfigSection::shardDatabase()).empty())
    {
        JLOG(m_journal.fatal())
            << "The [shard_db] configuration setting must be set";
        return false;
    }
    if (!shardStore_)
    {
        JLOG(m_journal.fatal()) << "Invalid [shard_db] configuration";
        return false;
    }
    shardStore_->validate();
    return true;
}

void
ApplicationImp::setMaxDisallowedLedger()
{
    boost::optional<LedgerIndex> seq;
    {
        auto db = getLedgerDB().checkoutDb();
        *db << "SELECT MAX(LedgerSeq) FROM Ledgers;", soci::into(seq);
    }
    if (seq)
        maxDisallowedLedger_ = *seq;

    JLOG(m_journal.trace())
        << "Max persisted ledger is " << maxDisallowedLedger_;
}

//------------------------------------------------------------------------------

Application::Application() : beast::PropertyStream::Source("app")
{
}

//------------------------------------------------------------------------------

std::unique_ptr<Application>
make_Application(
    std::unique_ptr<Config> config,
    std::unique_ptr<Logs> logs,
    std::unique_ptr<TimeKeeper> timeKeeper)
{
    return std::make_unique<ApplicationImp>(
        std::move(config), std::move(logs), std::move(timeKeeper));
}

}  // namespace ripple
