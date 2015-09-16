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

#include <BeastConfig.h>
#include <ripple/app/main/Application.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/app/main/DBInit.h>
#include <ripple/app/main/BasicApp.h>
#include <ripple/app/main/Tuning.h>
#include <ripple/app/ledger/AcceptedLedger.h>
#include <ripple/app/ledger/InboundLedgers.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/LedgerToJson.h>
#include <ripple/app/ledger/OpenLedger.h>
#include <ripple/app/ledger/OrderBookDB.h>
#include <ripple/app/ledger/PendingSaves.h>
#include <ripple/app/main/CollectorManager.h>
#include <ripple/app/main/LoadManager.h>
#include <ripple/app/main/LocalCredentials.h>
#include <ripple/app/main/NodeStoreScheduler.h>
#include <ripple/app/misc/AmendmentTable.h>
#include <ripple/app/misc/HashRouter.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/misc/SHAMapStore.h>
#include <ripple/app/misc/Validations.h>
#include <ripple/app/paths/FindPaths.h>
#include <ripple/app/paths/PathRequests.h>
#include <ripple/app/misc/UniqueNodeList.h>
#include <ripple/app/tx/InboundTransactions.h>
#include <ripple/app/tx/TransactionMaster.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/ResolverAsio.h>
#include <ripple/basics/Sustain.h>
#include <ripple/basics/chrono.h>
#include <ripple/json/json_reader.h>
#include <ripple/json/to_string.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/core/LoadFeeTrack.h>
#include <ripple/core/TimeKeeper.h>
#include <ripple/ledger/CachedSLEs.h>
#include <ripple/nodestore/Database.h>
#include <ripple/nodestore/DummyScheduler.h>
#include <ripple/nodestore/Manager.h>
#include <ripple/overlay/make_Overlay.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/protocol/STParsedJSON.h>
#include <ripple/protocol/types.h>
#include <ripple/server/make_ServerHandler.h>
#include <ripple/shamap/Family.h>
#include <ripple/unity/git_id.h>
#include <ripple/websocket/MakeServer.h>
#include <ripple/crypto/RandomNumbers.h>
#include <beast/asio/io_latency_probe.h>
#include <beast/module/core/text/LexicalCast.h>
#include <beast/module/core/thread/DeadlineTimer.h>
#include <boost/asio/signal_set.hpp>
#include <boost/optional.hpp>
#include <fstream>

namespace ripple {

// 204/256 about 80%
static int const MAJORITY_FRACTION (204);

// This hack lets the s_instance variable remain set during
// the call to ~Application
class ApplicationImpBase : public Application
{
public:
    ApplicationImpBase ()
    {
        assert (s_instance == nullptr);
        s_instance = this;
    }

    ~ApplicationImpBase ()
    {
        s_instance = nullptr;
    }

    static Application* s_instance;

    static Application& getInstance ()
    {
        bassert (s_instance != nullptr);
        return *s_instance;
    }
};

Application* ApplicationImpBase::s_instance;

//------------------------------------------------------------------------------

namespace detail {

class AppFamily : public Family
{
private:
    Application& app_;
    TreeNodeCache treecache_;
    FullBelowCache fullbelow_;
    NodeStore::Database& db_;

public:
    AppFamily (AppFamily const&) = delete;
    AppFamily& operator= (AppFamily const&) = delete;

    AppFamily (Application& app, NodeStore::Database& db,
            CollectorManager& collectorManager)
        : app_ (app)
        , treecache_ ("TreeNodeCache", 65536, 60, stopwatch(),
            deprecatedLogs().journal("TaggedCache"))
        , fullbelow_ ("full_below", stopwatch(),
            collectorManager.collector(),
                fullBelowTargetSize, fullBelowExpirationSeconds)
        , db_ (db)
    {
    }

    FullBelowCache&
    fullbelow() override
    {
        return fullbelow_;
    }

    FullBelowCache const&
    fullbelow() const override
    {
        return fullbelow_;
    }

    TreeNodeCache&
    treecache() override
    {
        return treecache_;
    }

    TreeNodeCache const&
    treecache() const override
    {
        return treecache_;
    }

    NodeStore::Database&
    db() override
    {
        return db_;
    }

    NodeStore::Database const&
    db() const override
    {
        return db_;
    }

    void
    missing_node (std::uint32_t seq) override
    {
        uint256 const hash = app_.getLedgerMaster ().getHashBySeq (seq);
        if (hash.isZero())
            app_.getInboundLedgers ().acquire (
                hash, seq, InboundLedger::fcGENERIC);
    }
};

} // detail

//------------------------------------------------------------------------------

// VFALCO TODO Move the function definitions into the class declaration
class ApplicationImp
    : public ApplicationImpBase
    , public beast::RootStoppable
    , public beast::DeadlineTimer::Listener
    , public BasicApp
{
private:
    class io_latency_sampler
    {
    private:
        std::mutex mutable m_mutex;
        beast::insight::Event m_event;
        beast::Journal m_journal;
        beast::io_latency_probe <std::chrono::steady_clock> m_probe;
        std::chrono::milliseconds m_lastSample;

    public:
        io_latency_sampler (
            beast::insight::Event ev,
            beast::Journal journal,
            std::chrono::milliseconds interval,
            boost::asio::io_service& ios)
            : m_event (ev)
            , m_journal (journal)
            , m_probe (interval, ios)
        {
        }

        void
        start()
        {
            m_probe.sample (std::ref(*this));
        }

        template <class Duration>
        void operator() (Duration const& elapsed)
        {
            auto const ms (ceil <std::chrono::milliseconds> (elapsed));

            {
                std::unique_lock <std::mutex> lock (m_mutex);
                m_lastSample = ms;
            }

            if (ms.count() >= 10)
                m_event.notify (ms);
            if (ms.count() >= 500)
                m_journal.warning <<
                    "io_service latency = " << ms;
        }

        std::chrono::milliseconds
        get () const
        {
            std::unique_lock <std::mutex> lock (m_mutex);
            return m_lastSample;
        }

        void
        cancel ()
        {
            m_probe.cancel ();
        }

        void cancel_async ()
        {
            m_probe.cancel_async ();
        }
    };

public:
    Config const& config_;
    Logs& m_logs;
    beast::Journal m_journal;
    Application::MutexType m_masterMutex;

    std::unique_ptr<TimeKeeper> timeKeeper_;

    // Required by the SHAMapStore
    TransactionMaster m_txMaster;

    NodeStoreScheduler m_nodeStoreScheduler;
    std::unique_ptr <SHAMapStore> m_shaMapStore;
    std::unique_ptr <NodeStore::Database> m_nodeStore;
    PendingSaves pendingSaves_;
    AccountIDCache accountIDCache_;
    boost::optional<OpenLedger> openLedger_;

    // These are not Stoppable-derived
    NodeCache m_tempNodeCache;
    std::unique_ptr <CollectorManager> m_collectorManager;
    detail::AppFamily family_;
    CachedSLEs cachedSLEs_;
    LocalCredentials m_localCredentials;

    std::unique_ptr <Resource::Manager> m_resourceManager;

    // These are Stoppable-related
    std::unique_ptr <JobQueue> m_jobQueue;
    // VFALCO TODO Make OrderBookDB abstract
    OrderBookDB m_orderBookDB;
    std::unique_ptr <PathRequests> m_pathRequests;
    std::unique_ptr <LedgerMaster> m_ledgerMaster;
    std::unique_ptr <InboundLedgers> m_inboundLedgers;
    std::unique_ptr <InboundTransactions> m_inboundTransactions;
    TaggedCache <uint256, AcceptedLedger> m_acceptedLedgerCache;
    std::unique_ptr <NetworkOPs> m_networkOPs;
    std::unique_ptr <UniqueNodeList> m_deprecatedUNL;
    std::unique_ptr <ServerHandler> serverHandler_;
    std::unique_ptr <AmendmentTable> m_amendmentTable;
    std::unique_ptr <LoadFeeTrack> mFeeTrack;
    std::unique_ptr <HashRouter> mHashRouter;
    std::unique_ptr <Validations> mValidations;
    std::unique_ptr <LoadManager> m_loadManager;
    beast::DeadlineTimer m_sweepTimer;
    beast::DeadlineTimer m_entropyTimer;

    std::unique_ptr <DatabaseCon> mTxnDB;
    std::unique_ptr <DatabaseCon> mLedgerDB;
    std::unique_ptr <DatabaseCon> mWalletDB;
    std::unique_ptr <Overlay> m_overlay;
    std::vector <std::unique_ptr<beast::Stoppable>> websocketServers_;

    boost::asio::signal_set m_signals;
    beast::WaitableEvent m_stop;

    std::unique_ptr <ResolverAsio> m_resolver;

    io_latency_sampler m_io_latency_sampler;

    //--------------------------------------------------------------------------

    static
    std::size_t
    numberOfThreads(Config const& config)
    {
    #if RIPPLE_SINGLE_IO_SERVICE_THREAD
        return 1;
    #else
        return (config.NODE_SIZE >= 2) ? 2 : 1;
    #endif
    }

    //--------------------------------------------------------------------------

    ApplicationImp (Config const& config, Logs& logs)
        : RootStoppable ("Application")
        , BasicApp (numberOfThreads(config))
        , config_ (config)
        , m_logs (logs)

        , m_journal (m_logs.journal("Application"))

        , timeKeeper_ (make_TimeKeeper(
            deprecatedLogs().journal("TimeKeeper")))

        , m_txMaster (*this)

        , m_nodeStoreScheduler (*this)

        , m_shaMapStore (make_SHAMapStore (*this, setup_SHAMapStore (
                config_), *this, m_nodeStoreScheduler,
                m_logs.journal ("SHAMapStore"), m_logs.journal ("NodeObject"),
                m_txMaster, config_))

        , m_nodeStore (m_shaMapStore->makeDatabase ("NodeStore.main", 4))

        , accountIDCache_(128000)

        , m_tempNodeCache ("NodeCache", 16384, 90, stopwatch(),
            m_logs.journal("TaggedCache"))

        , m_collectorManager (CollectorManager::New (
            config_.section (SECTION_INSIGHT), m_logs.journal("Collector")))

        , family_ (*this, *m_nodeStore, *m_collectorManager)

        , cachedSLEs_ (std::chrono::minutes(1), stopwatch())

        , m_localCredentials (*this)

        , m_resourceManager (Resource::make_Manager (
            m_collectorManager->collector(), m_logs.journal("Resource")))

        // The JobQueue has to come pretty early since
        // almost everything is a Stoppable child of the JobQueue.
        //
        , m_jobQueue (make_JobQueue (m_collectorManager->group ("jobq"),
            m_nodeStoreScheduler, m_logs.journal("JobQueue")))

        //
        // Anything which calls addJob must be a descendant of the JobQueue
        //

        , m_orderBookDB (*this, *m_jobQueue)

        , m_pathRequests (std::make_unique<PathRequests> (
            *this, m_logs.journal("PathRequest"), m_collectorManager->collector ()))

        , m_ledgerMaster (make_LedgerMaster (*this, stopwatch (),
            *m_jobQueue, m_collectorManager->collector (),
            m_logs.journal("LedgerMaster")))

        // VFALCO NOTE must come before NetworkOPs to prevent a crash due
        //             to dependencies in the destructor.
        //
        , m_inboundLedgers (make_InboundLedgers (*this, stopwatch(),
            *m_jobQueue, m_collectorManager->collector ()))

        , m_inboundTransactions (make_InboundTransactions
            ( *this, stopwatch()
            , *m_jobQueue
            , m_collectorManager->collector ()
            , [this](uint256 const& setHash,
                std::shared_ptr <SHAMap> const& set)
            {
                gotTXSet (setHash, set);
            }))

        , m_acceptedLedgerCache ("AcceptedLedger", 4, 60, stopwatch(),
            m_logs.journal("TaggedCache"))

        , m_networkOPs (make_NetworkOPs (*this, stopwatch(),
            config_.RUN_STANDALONE, config_.NETWORK_QUORUM,
            *m_jobQueue, *m_ledgerMaster, *m_jobQueue,
            m_logs.journal("NetworkOPs")))

        // VFALCO NOTE LocalCredentials starts the deprecated UNL service
        , m_deprecatedUNL (make_UniqueNodeList (*this, *m_jobQueue))

        , serverHandler_ (make_ServerHandler (*this, *m_networkOPs, get_io_service (),
            *m_jobQueue, *m_networkOPs, *m_resourceManager, *m_collectorManager))

        , m_amendmentTable (make_AmendmentTable
                            (weeks(2), MAJORITY_FRACTION,
                             m_logs.journal("AmendmentTable")))

        , mFeeTrack (std::make_unique<LoadFeeTrack>(m_logs.journal("LoadManager")))

        , mHashRouter (std::make_unique<HashRouter>(
            HashRouter::getDefaultHoldTime ()))

        , mValidations (make_Validations (*this))

        , m_loadManager (make_LoadManager (*this, *this, m_logs.journal("LoadManager")))

        , m_sweepTimer (this)

        , m_entropyTimer (this)

        , m_signals (get_io_service())

        , m_resolver (ResolverAsio::New (get_io_service(), m_logs.journal("Resolver")))

        , m_io_latency_sampler (m_collectorManager->collector()->make_event ("ios_latency"),
            m_logs.journal("Application"), std::chrono::milliseconds (100), get_io_service())
    {
        add (m_resourceManager.get ());

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
        m_nodeStoreScheduler.setJobQueue (*m_jobQueue);

        add (m_ledgerMaster->getPropertySource ());
        add (*serverHandler_);
    }

    //--------------------------------------------------------------------------

    CollectorManager& getCollectorManager ()
    {
        return *m_collectorManager;
    }

    Family&
    family()
    {
        return family_;
    }

    TimeKeeper&
    timeKeeper()
    {
        return *timeKeeper_;
    }

    JobQueue& getJobQueue ()
    {
        return *m_jobQueue;
    }

    LocalCredentials& getLocalCredentials ()
    {
        return m_localCredentials ;
    }

    NetworkOPs& getOPs ()
    {
        return *m_networkOPs;
    }

    Config const&
    config() const override
    {
        return config_;
    }

    boost::asio::io_service& getIOService ()
    {
        return get_io_service();
    }

    std::chrono::milliseconds getIOLatency ()
    {
        std::unique_lock <std::mutex> m_IOLatencyLock;

        return m_io_latency_sampler.get ();
    }

    LedgerMaster& getLedgerMaster ()
    {
        return *m_ledgerMaster;
    }

    InboundLedgers& getInboundLedgers ()
    {
        return *m_inboundLedgers;
    }

    InboundTransactions& getInboundTransactions ()
    {
        return *m_inboundTransactions;
    }

    TaggedCache <uint256, AcceptedLedger>& getAcceptedLedgerCache ()
    {
        return m_acceptedLedgerCache;
    }

    void gotTXSet (uint256 const& setHash, std::shared_ptr<SHAMap> const& set)
    {
        m_networkOPs->mapComplete (setHash, set);
    }

    TransactionMaster& getMasterTransaction ()
    {
        return m_txMaster;
    }

    NodeCache& getTempNodeCache ()
    {
        return m_tempNodeCache;
    }

    NodeStore::Database& getNodeStore ()
    {
        return *m_nodeStore;
    }

    Application::MutexType& getMasterMutex ()
    {
        return m_masterMutex;
    }

    LoadManager& getLoadManager ()
    {
        return *m_loadManager;
    }

    Resource::Manager& getResourceManager ()
    {
        return *m_resourceManager;
    }

    OrderBookDB& getOrderBookDB ()
    {
        return m_orderBookDB;
    }

    PathRequests& getPathRequests ()
    {
        return *m_pathRequests;
    }

    CachedSLEs&
    cachedSLEs()
    {
        return cachedSLEs_;
    }

    AmendmentTable& getAmendmentTable()
    {
        return *m_amendmentTable;
    }

    LoadFeeTrack& getFeeTrack ()
    {
        return *mFeeTrack;
    }

    HashRouter& getHashRouter ()
    {
        return *mHashRouter;
    }

    Validations& getValidations ()
    {
        return *mValidations;
    }

    UniqueNodeList& getUNL ()
    {
        return *m_deprecatedUNL;
    }

    SHAMapStore& getSHAMapStore () override
    {
        return *m_shaMapStore;
    }

    PendingSaves& pendingSaves() override
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

    Overlay& overlay ()
    {
        return *m_overlay;
    }

    // VFALCO TODO Move these to the .cpp
    bool running ()
    {
        return mTxnDB != nullptr;
    }

    DatabaseCon& getTxnDB ()
    {
        assert (mTxnDB.get() != nullptr);
        return *mTxnDB;
    }
    DatabaseCon& getLedgerDB ()
    {
        assert (mLedgerDB.get() != nullptr);
        return *mLedgerDB;
    }
    DatabaseCon& getWalletDB ()
    {
        assert (mWalletDB.get() != nullptr);
        return *mWalletDB;
    }

    bool isShutdown ()
    {
        // from Stoppable mixin
        return isStopped();
    }

    bool serverOkay (std::string& reason) override;

    //--------------------------------------------------------------------------
    bool initSqliteDbs ()
    {
        assert (mTxnDB.get () == nullptr);
        assert (mLedgerDB.get () == nullptr);
        assert (mWalletDB.get () == nullptr);

        DatabaseCon::Setup setup = setup_DatabaseCon (config_);
        mTxnDB = std::make_unique <DatabaseCon> (setup, "transaction.db",
                TxnDBInit, TxnDBCount);
        mLedgerDB = std::make_unique <DatabaseCon> (setup, "ledger.db",
                LedgerDBInit, LedgerDBCount);
        mWalletDB = std::make_unique <DatabaseCon> (setup, "wallet.db",
                WalletDBInit, WalletDBCount);

        return
            mTxnDB.get () != nullptr &&
            mLedgerDB.get () != nullptr &&
            mWalletDB.get () != nullptr;
    }

    void signalled(const boost::system::error_code& ec, int signal_number)
    {
        if (ec == boost::asio::error::operation_aborted)
        {
            // Indicates the signal handler has been aborted
            // do nothing
        }
        else if (ec)
        {
            m_journal.error << "Received signal: " << signal_number
                            << " with error: " << ec.message();
        }
        else
        {
            m_journal.debug << "Received signal: " << signal_number;
            signalStop();
        }
    }

    // VFALCO TODO Break this function up into many small initialization segments.
    //             Or better yet refactor these initializations into RAII classes
    //             which are members of the Application object.
    //
    void setup ()
    {
        // VFALCO NOTE: 0 means use heuristics to determine the thread count.
        m_jobQueue->setThreadCount (0, config_.RUN_STANDALONE);

        // We want to intercept and wait for CTRL-C to terminate the process
        m_signals.add (SIGINT);

        m_signals.async_wait(std::bind(&ApplicationImp::signalled, this,
            std::placeholders::_1, std::placeholders::_2));

        assert (mTxnDB == nullptr);

        auto debug_log = config_.getDebugLogFile ();

        if (!debug_log.empty ())
        {
            // Let debug messages go to the file but only WARNING or higher to
            // regular output (unless verbose)

            if (!m_logs.open(debug_log))
                std::cerr << "Can't open log file " << debug_log << '\n';

            if (m_logs.severity() > beast::Journal::kDebug)
                m_logs.severity (beast::Journal::kDebug);
        }

        if (!config_.RUN_STANDALONE)
            timeKeeper_->run(config_.SNTP_SERVERS);

        if (!initSqliteDbs ())
        {
            m_journal.fatal << "Can not create database connections!";
            exitWithCode(3);
        }

        getLedgerDB ().getSession ()
            << boost::str (boost::format ("PRAGMA cache_size=-%d;") %
                           (config_.getSize (siLgrDBCache) * 1024));

        getTxnDB ().getSession ()
                << boost::str (boost::format ("PRAGMA cache_size=-%d;") %
                               (config_.getSize (siTxnDBCache) * 1024));

        mTxnDB->setupCheckpointing (m_jobQueue.get());
        mLedgerDB->setupCheckpointing (m_jobQueue.get());

        if (!config_.RUN_STANDALONE)
            updateTables ();

        m_amendmentTable->addInitial (
            config_.section (SECTION_AMENDMENTS));
        initializePathfinding ();

        m_ledgerMaster->setMinValidations (config_.VALIDATION_QUORUM);

        auto const startUp = config_.START_UP;
        if (startUp == Config::FRESH)
        {
            m_journal.info << "Starting new Ledger";

            startGenesisLedger ();
        }
        else if (startUp == Config::LOAD ||
                 startUp == Config::LOAD_FILE ||
                 startUp == Config::REPLAY)
        {
            m_journal.info << "Loading specified Ledger";

            if (!loadOldLedger (config_.START_LEDGER,
                                startUp == Config::REPLAY,
                                startUp == Config::LOAD_FILE))
            {
                exitWithCode(-1);
            }
        }
        else if (startUp == Config::NETWORK)
        {
            // This should probably become the default once we have a stable network.
            if (!config_.RUN_STANDALONE)
                m_networkOPs->needNetworkLedger ();

            startGenesisLedger ();
        }
        else
        {
            startGenesisLedger ();
        }

        m_orderBookDB.setup (getLedgerMaster ().getCurrentLedger ());

        // Begin validation and ip maintenance.
        //
        // - LocalCredentials maintains local information: including identity
        // - and network connection persistence information.
        //
        // VFALCO NOTE this starts the UNL
        m_localCredentials.start ();

        //
        // Set up UNL.
        //
        if (!config_.RUN_STANDALONE)
            getUNL ().nodeBootstrap ();

        mValidations->tune (config_.getSize (siValidationsSize), config_.getSize (siValidationsAge));
        m_nodeStore->tune (config_.getSize (siNodeCacheSize), config_.getSize (siNodeCacheAge));
        m_ledgerMaster->tune (config_.getSize (siLedgerSize), config_.getSize (siLedgerAge));
        family().treecache().setTargetSize (config_.getSize (siTreeCacheSize));
        family().treecache().setTargetAge (config_.getSize (siTreeCacheAge));

        //----------------------------------------------------------------------
        //
        // Server
        //
        //----------------------------------------------------------------------

        // VFALCO NOTE Unfortunately, in stand-alone mode some code still
        //             foolishly calls overlay(). When this is fixed we can
        //             move the instantiation inside a conditional:
        //
        //             if (!config_.RUN_STANDALONE)
        m_overlay = make_Overlay (*this, setup_Overlay(config_), *m_jobQueue,
            *serverHandler_, *m_resourceManager, *m_resolver, get_io_service(),
            config_);
        add (*m_overlay); // add to PropertyStream

        m_overlay->setupValidatorKeyManifests (config_, getWalletDB ());

        {
            auto setup = setup_ServerHandler(config_, std::cerr);
            setup.makeContexts();
            serverHandler_->setup (setup, m_journal);
        }

        // Create websocket servers.
        for (auto const& port : serverHandler_->setup().ports)
        {
            if (! port.websockets())
                continue;
            auto server = websocket::makeServer (
                {*this, port, *m_resourceManager, getOPs(), m_journal, config_,
                 *m_collectorManager});
            if (!server)
            {
                m_journal.fatal << "Could not create Websocket for [" <<
                    port.name << "]";
                throw std::exception();
            }
            websocketServers_.emplace_back (std::move (server));
        }

        //----------------------------------------------------------------------

        // Begin connecting to network.
        if (!config_.RUN_STANDALONE)
        {
            // Should this message be here, conceptually? In theory this sort
            // of message, if displayed, should be displayed from PeerFinder.
            if (config_.PEER_PRIVATE && config_.IPS.empty ())
                m_journal.warning << "No outbound peer connections will be made";

            // VFALCO NOTE the state timer resets the deadlock detector.
            //
            m_networkOPs->setStateTimer ();
        }
        else
        {
            m_journal.warning << "Running in standalone mode";

            m_networkOPs->setStandAlone ();
        }
    }

    //--------------------------------------------------------------------------
    //
    // Stoppable
    //

    void onPrepare() override
    {
    }

    void onStart ()
    {
        m_journal.info << "Application starting. Build is " << gitCommitID();

        m_sweepTimer.setExpiration (10);
        m_entropyTimer.setRecurringExpiration (300);

        m_io_latency_sampler.start();

        m_resolver->start ();
    }

    // Called to indicate shutdown.
    void onStop ()
    {
        m_journal.debug << "Application stopping";

        m_io_latency_sampler.cancel_async ();

        // VFALCO Enormous hack, we have to force the probe to cancel
        //        before we stop the io_service queue or else it never
        //        unblocks in its destructor. The fix is to make all
        //        io_objects gracefully handle exit so that we can
        //        naturally return from io_service::run() instead of
        //        forcing a call to io_service::stop()
        m_io_latency_sampler.cancel ();

        m_resolver->stop_async ();

        // NIKB This is a hack - we need to wait for the resolver to
        //      stop. before we stop the io_server_queue or weird
        //      things will happen.
        m_resolver->stop ();

        m_sweepTimer.cancel ();

        m_entropyTimer.cancel ();

        mValidations->flush ();

        m_overlay->saveValidatorKeyManifests (getWalletDB ());

        stopped ();
    }

    //------------------------------------------------------------------------------
    //
    // PropertyStream
    //

    void onWrite (beast::PropertyStream::Map& stream)
    {
    }

    //------------------------------------------------------------------------------

    void run ()
    {
        // VFALCO NOTE I put this here in the hopes that when unit tests run (which
        //             tragically require an Application object to exist or else they
        //             crash), the run() function will not get called and we will
        //             avoid doing silly things like contacting the SNTP server, or
        //             running the various logic threads like Validators, PeerFinder, etc.
        prepare ();
        start ();


        {
            if (!config_.RUN_STANDALONE)
            {
                // VFALCO NOTE This seems unnecessary. If we properly refactor the load
                //             manager then the deadlock detector can just always be "armed"
                //
                getLoadManager ().activateDeadlockDetector ();
            }
        }

        m_stop.wait ();

        // Stop the server. When this returns, all
        // Stoppable objects should be stopped.
        m_journal.info << "Received shutdown request";
        stop (m_journal);
        m_journal.info << "Done.";
        StopSustain();
    }

    void exitWithCode(int code)
    {
        StopSustain();
        // VFALCO This breaks invariants: automatic objects
        //        will not have destructors called.
        std::exit(code);
    }

    void signalStop ()
    {
        // Unblock the main thread (which is sitting in run()).
        //
        m_stop.signal();
    }

    void onDeadlineTimer (beast::DeadlineTimer& timer)
    {
        if (timer == m_entropyTimer)
        {
            add_entropy (nullptr, 0);
            return;
        }

        if (timer == m_sweepTimer)
        {
            // VFALCO TODO Move all this into doSweep

            boost::filesystem::space_info space =
                    boost::filesystem::space (config_.legacy ("database_path"));

            // VFALCO TODO Give this magic constant a name and move it into a well documented header
            //
            if (space.available < (512 * 1024 * 1024))
            {
                m_journal.fatal << "Remaining free disk space is less than 512MB";
                signalStop ();
            }

            m_jobQueue->addJob(jtSWEEP, "sweep", [this] (Job&) { doSweep(); });
        }
    }

    void doSweep ()
    {
        // VFALCO NOTE Does the order of calls matter?
        // VFALCO TODO fix the dependency inversion using an observer,
        //         have listeners register for "onSweep ()" notification.

        family().fullbelow().sweep ();
        getMasterTransaction().sweep();
        getNodeStore().sweep();
        getLedgerMaster().sweep();
        getTempNodeCache().sweep();
        getValidations().sweep();
        getInboundLedgers().sweep();
        m_acceptedLedgerCache.sweep();
        family().treecache().sweep();
        cachedSLEs_.expire();

        // VFALCO NOTE does the call to sweep() happen on another thread?
        m_sweepTimer.setExpiration (config_.getSize (siSweepInterval));
    }


private:
    void addTxnSeqField();
    void updateTables ();
    void startGenesisLedger ();
    Ledger::pointer getLastFullLedger();
    bool loadOldLedger (
        std::string const& ledgerID, bool replay, bool isFilename);

    void onAnnounceAddress ();
};

//------------------------------------------------------------------------------

void ApplicationImp::startGenesisLedger ()
{
    std::shared_ptr<Ledger const> const genesis =
        std::make_shared<Ledger>(
            create_genesis, config_, family());
    auto const next = std::make_shared<Ledger>(
        open_ledger, *genesis);
    next->setClosed ();
    next->setImmutable ();
    m_networkOPs->setLastCloseTime (next->info().closeTime);
    openLedger_.emplace(next, config_,
        cachedSLEs_, deprecatedLogs().journal("OpenLedger"));
    m_ledgerMaster->pushLedger (next,
        std::make_shared<Ledger>(open_ledger, *next));
}

Ledger::pointer
ApplicationImp::getLastFullLedger()
{
    try
    {
        Ledger::pointer ledger;
        std::uint32_t ledgerSeq;
        uint256 ledgerHash;
        std::tie (ledger, ledgerSeq, ledgerHash) =
                loadLedgerHelper ("order by LedgerSeq desc limit 1", *this);

        if (!ledger)
            return ledger;

        ledger->setClosed ();
        ledger->setImmutable();

        if (getLedgerMaster ().haveLedger (ledgerSeq))
            ledger->setValidated ();

        if (ledger->getHash () != ledgerHash)
        {
            if (ShouldLog (lsERROR, Ledger))
            {
                WriteLog (lsERROR, Ledger) << "Failed on ledger";
                Json::Value p;
                addJson (p, {*ledger, LedgerFill::full});
                WriteLog (lsERROR, Ledger) << p;
            }

            assert (false);
            return Ledger::pointer ();
        }

        WriteLog (lsTRACE, Ledger) << "Loaded ledger: " << ledgerHash;
        return ledger;
    }
    catch (SHAMapMissingNode& sn)
    {
        WriteLog (lsWARNING, Ledger)
                << "Database contains ledger with missing nodes: " << sn;
        return Ledger::pointer ();
    }
}

bool ApplicationImp::loadOldLedger (
    std::string const& ledgerID, bool replay, bool isFileName)
{
    try
    {
        Ledger::pointer loadLedger, replayLedger;

        if (isFileName)
        {
            std::ifstream ledgerFile (ledgerID.c_str (), std::ios::in);
            if (!ledgerFile)
            {
                m_journal.fatal << "Unable to open file";
            }
            else
            {
                 Json::Reader reader;
                 Json::Value jLedger;
                 if (!reader.parse (ledgerFile, jLedger))
                     m_journal.fatal << "Unable to parse ledger JSON";
                 else
                 {
                     std::reference_wrapper<Json::Value> ledger (jLedger);

                     // accept a wrapped ledger
                     if (ledger.get().isMember  ("result"))
                         ledger = ledger.get()["result"];
                     if (ledger.get().isMember ("ledger"))
                         ledger = ledger.get()["ledger"];


                     std::uint32_t seq = 1;
                     auto closeTime = timeKeeper().closeTime().time_since_epoch().count();
                     std::uint32_t closeTimeResolution = 30;
                     bool closeTimeEstimated = false;
                     std::uint64_t totalDrops = 0;

                     if (ledger.get().isMember ("accountState"))
                     {
                          if (ledger.get().isMember (jss::ledger_index))
                          {
                              seq = ledger.get()[jss::ledger_index].asUInt();
                          }
                          if (ledger.get().isMember ("close_time"))
                          {
                              closeTime = ledger.get()["close_time"].asUInt();
                          }
                          if (ledger.get().isMember ("close_time_resolution"))
                          {
                              closeTimeResolution =
                                  ledger.get()["close_time_resolution"].asUInt();
                          }
                          if (ledger.get().isMember ("close_time_estimated"))
                          {
                              closeTimeEstimated =
                                  ledger.get()["close_time_estimated"].asBool();
                          }
                          if (ledger.get().isMember ("total_coins"))
                          {
                              totalDrops =
                                beast::lexicalCastThrow<std::uint64_t>
                                    (ledger.get()["total_coins"].asString());
                          }
                         ledger = ledger.get()["accountState"];
                     }
                     if (!ledger.get().isArray ())
                     {
                         m_journal.fatal << "State nodes must be an array";
                     }
                     else
                     {
                         loadLedger = std::make_shared<Ledger> (seq, closeTime, config_, family());
                         loadLedger->setTotalDrops(totalDrops);

                         for (Json::UInt index = 0; index < ledger.get().size(); ++index)
                         {
                             Json::Value& entry = ledger.get()[index];

                             uint256 uIndex;
                             uIndex.SetHex (entry[jss::index].asString());
                             entry.removeMember (jss::index);

                             STParsedJSONObject stp ("sle", ledger.get()[index]);
                             // m_journal.info << "json: " << stp.object->getJson(0);

                             if (stp.object && (uIndex.isNonZero()))
                             {
                                 // VFALCO TODO This is the only place that
                                 //             constructor is used, try to remove it
                                 STLedgerEntry sle (*stp.object, uIndex);
                                 bool ok = loadLedger->addSLE (sle);
                                 if (!ok)
                                     m_journal.warning << "Couldn't add serialized ledger: " << uIndex;
                             }
                             else
                             {
                                 m_journal.warning << "Invalid entry in ledger";
                             }
                         }

                         loadLedger->setClosed ();
                         loadLedger->setAccepted (closeTime,
                             closeTimeResolution, ! closeTimeEstimated);
                     }
                 }
            }
        }
        else if (ledgerID.empty () || (ledgerID == "latest"))
        {
            loadLedger = getLastFullLedger ();
        }
        else if (ledgerID.length () == 64)
        {
            // by hash
            uint256 hash;
            hash.SetHex (ledgerID);
            loadLedger = loadByHash (hash, *this);

            if (!loadLedger)
            {
                // Try to build the ledger from the back end
                auto il = std::make_shared <InboundLedger> (
                    *this, hash, 0, InboundLedger::fcGENERIC, stopwatch());
                if (il->checkLocal ())
                    loadLedger = il->getLedger ();
            }

        }
        else // assume by sequence
            loadLedger = loadByIndex (
                beast::lexicalCastThrow <std::uint32_t> (ledgerID), *this);

        if (!loadLedger)
        {
            m_journal.fatal << "No Ledger found from ledgerID="
                            << ledgerID << std::endl;
            return false;
        }

        if (replay)
        {
            // Replay a ledger close with same prior ledger and transactions

            // this ledger holds the transactions we want to replay
            replayLedger = loadLedger;

            m_journal.info << "Loading parent ledger";

            loadLedger = loadByHash (replayLedger->info().parentHash, *this);
            if (!loadLedger)
            {
                m_journal.info << "Loading parent ledger from node store";

                // Try to build the ledger from the back end
                auto il = std::make_shared <InboundLedger> (
                    *this, replayLedger->info().parentHash, 0, InboundLedger::fcGENERIC,
                    stopwatch());
                if (il->checkLocal ())
                    loadLedger = il->getLedger ();

                if (!loadLedger)
                {
                    m_journal.fatal << "Replay ledger missing/damaged";
                    assert (false);
                    return false;
                }
            }
        }

        loadLedger->setClosed ();

        m_journal.info << "Loading ledger " << loadLedger->getHash () << " seq:" << loadLedger->info().seq;

        if (loadLedger->info().accountHash.isZero ())
        {
            m_journal.fatal << "Ledger is empty.";
            assert (false);
            return false;
        }

        if (!loadLedger->walkLedger ())
        {
            m_journal.fatal << "Ledger is missing nodes.";
            assert(false);
            return false;
        }

        if (!loadLedger->assertSane ())
        {
            m_journal.fatal << "Ledger is not sane.";
            assert(false);
            return false;
        }

        m_ledgerMaster->setLedgerRangePresent (loadLedger->info().seq, loadLedger->info().seq);

        auto const openLedger =
            std::make_shared<Ledger>(open_ledger, *loadLedger);
        m_ledgerMaster->switchLedgers (loadLedger, openLedger);
        m_ledgerMaster->forceValid(loadLedger);
        m_networkOPs->setLastCloseTime (loadLedger->info().closeTime);
        openLedger_.emplace(loadLedger, config_,
            cachedSLEs_, deprecatedLogs().journal("OpenLedger"));

        if (replay)
        {
            // inject transaction(s) from the replayLedger into our open ledger
            auto const& txns = replayLedger->txMap();

            // Get a mutable snapshot of the open ledger
            Ledger::pointer cur = getLedgerMaster().getCurrentLedger();
            cur = std::make_shared <Ledger> (*cur, true);
            assert (!cur->isImmutable());

            for (auto const& item : txns)
            {
                auto const txn =
                    replayLedger->txRead(item.key()).first;
                if (m_journal.info) m_journal.info <<
                    txn->getJson(0);
                Serializer s;
                txn->add(s);
                cur->rawTxInsert(item.key(),
                    std::make_shared<Serializer const>(
                        std::move(s)), nullptr);
                getHashRouter().setFlags (item.key(), SF_SIGGOOD);
            }

            // Switch to the mutable snapshot
            m_ledgerMaster->switchLedgers (loadLedger, cur);
        }
    }
    catch (SHAMapMissingNode&)
    {
        m_journal.fatal << "Data is missing for selected ledger";
        return false;
    }
    catch (boost::bad_lexical_cast&)
    {
        m_journal.fatal << "Ledger specified '" << ledgerID << "' is not valid";
        return false;
    }

    return true;
}

bool ApplicationImp::serverOkay (std::string& reason)
{
    if (! config().ELB_SUPPORT)
        return true;

    if (isShutdown ())
    {
        reason = "Server is shutting down";
        return false;
    }

    if (getOPs ().isNeedNetworkLedger ())
    {
        reason = "Not synchronized with network yet";
        return false;
    }

    if (getOPs ().getOperatingMode () < NetworkOPs::omSYNCING)
    {
        reason = "Not synchronized with network";
        return false;
    }

    if (!getLedgerMaster().isCaughtUp(reason))
        return false;

    if (getFeeTrack ().isLoadedLocal ())
    {
        reason = "Too much load";
        return false;
    }

    if (getOPs ().isAmendmentBlocked ())
    {
        reason = "Server version too old";
        return false;
    }

    return true;
}

//VFALCO TODO clean this up since it is just a file holding a single member function definition

static std::vector<std::string> getSchema (DatabaseCon& dbc, std::string const& dbName)
{
    std::vector<std::string> schema;
    schema.reserve(32);

    std::string sql = "SELECT sql FROM sqlite_master WHERE tbl_name='";
    sql += dbName;
    sql += "';";

    std::string r;
    soci::statement st = (dbc.getSession ().prepare << sql,
                          soci::into(r));
    st.execute ();
    while (st.fetch ())
    {
        schema.emplace_back (r);
    }

    return schema;
}

static bool schemaHas (DatabaseCon& dbc, std::string const& dbName, int line, std::string const& content)
{
    std::vector<std::string> schema = getSchema (dbc, dbName);

    if (static_cast<int> (schema.size ()) <= line)
    {
        WriteLog (lsFATAL, Application) << "Schema for " << dbName << " has too few lines";
        throw std::runtime_error ("bad schema");
    }

    return schema[line].find (content) != std::string::npos;
}

void ApplicationImp::addTxnSeqField ()
{
    if (schemaHas (getTxnDB (), "AccountTransactions", 0, "TxnSeq"))
        return;

    WriteLog (lsWARNING, Application) << "Transaction sequence field is missing";

    auto& session = getTxnDB ().getSession ();

    std::vector< std::pair<uint256, int> > txIDs;
    txIDs.reserve (300000);

    WriteLog (lsINFO, Application) << "Parsing transactions";
    int i = 0;
    uint256 transID;

    boost::optional<std::string> strTransId;
    soci::blob sociTxnMetaBlob(session);
    soci::indicator tmi;
    Blob txnMeta;

    soci::statement st =
            (session.prepare <<
             "SELECT TransID, TxnMeta FROM Transactions;",
             soci::into(strTransId),
             soci::into(sociTxnMetaBlob, tmi));

    st.execute ();
    while (st.fetch ())
    {
        if (soci::i_ok == tmi)
            convert (sociTxnMetaBlob, txnMeta);
        else
            txnMeta.clear ();

        std::string tid = strTransId.value_or("");
        transID.SetHex (tid, true);

        if (txnMeta.size () == 0)
        {
            txIDs.push_back (std::make_pair (transID, -1));
            WriteLog (lsINFO, Application) << "No metadata for " << transID;
        }
        else
        {
            TxMeta m (transID, 0, txnMeta);
            txIDs.push_back (std::make_pair (transID, m.getIndex ()));
        }

        if ((++i % 1000) == 0)
        {
            WriteLog (lsINFO, Application) << i << " transactions read";
        }
    }

    WriteLog (lsINFO, Application) << "All " << i << " transactions read";

    soci::transaction tr(session);

    WriteLog (lsINFO, Application) << "Dropping old index";
    session << "DROP INDEX AcctTxIndex;";

    WriteLog (lsINFO, Application) << "Altering table";
    session << "ALTER TABLE AccountTransactions ADD COLUMN TxnSeq INTEGER;";

    boost::format fmt ("UPDATE AccountTransactions SET TxnSeq = %d WHERE TransID = '%s';");
    i = 0;
    for (auto& t : txIDs)
    {
        session << boost::str (fmt % t.second % to_string (t.first));

        if ((++i % 1000) == 0)
        {
            WriteLog (lsINFO, Application) << i << " transactions updated";
        }
    }

    WriteLog (lsINFO, Application) << "Building new index";
    session << "CREATE INDEX AcctTxIndex ON AccountTransactions(Account, LedgerSeq, TxnSeq, TransID);";

    tr.commit ();
}

void ApplicationImp::updateTables ()
{
    if (config_.section (ConfigSection::nodeDatabase ()).empty ())
    {
        WriteLog (lsFATAL, Application) << "The [node_db] configuration setting has been updated and must be set";
        exitWithCode(1);
    }

    // perform any needed table updates
    assert (schemaHas (getTxnDB (), "AccountTransactions", 0, "TransID"));
    assert (!schemaHas (getTxnDB (), "AccountTransactions", 0, "foobar"));
    addTxnSeqField ();

    if (schemaHas (getTxnDB (), "AccountTransactions", 0, "PRIMARY"))
    {
        WriteLog (lsFATAL, Application) << "AccountTransactions database should not have a primary key";
        exitWithCode(1);
    }

    if (config_.doImport)
    {
        NodeStore::DummyScheduler scheduler;
        std::unique_ptr <NodeStore::Database> source =
            NodeStore::Manager::instance().make_Database ("NodeStore.import", scheduler,
                deprecatedLogs().journal("NodeObject"), 0,
                config_[ConfigSection::importNodeDatabase ()]);

        WriteLog (lsWARNING, NodeObject) <<
            "Node import from '" << source->getName () << "' to '"
                                 << getNodeStore().getName () << "'.";

        getNodeStore().import (*source);
    }
}

void ApplicationImp::onAnnounceAddress ()
{
    // NIKB CODEME
}

//------------------------------------------------------------------------------

Application::Application ()
    : beast::PropertyStream::Source ("app")
{
}

std::unique_ptr<Application>
make_Application (Config const& config, Logs& logs)
{
    return std::make_unique<ApplicationImp>(
        config, logs);
}

Application& getApp ()
{
    return ApplicationImpBase::getInstance ();
}

}
