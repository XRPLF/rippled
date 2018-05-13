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

#include <ripple/app/main/Application.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/app/consensus/RCLValidations.h>
#include <ripple/app/main/DBInit.h>
#include <ripple/app/main/BasicApp.h>
#include <ripple/app/main/Tuning.h>
#include <ripple/app/ledger/InboundLedgers.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/LedgerToJson.h>
#include <ripple/app/ledger/OpenLedger.h>
#include <ripple/app/ledger/OrderBookDB.h>
#include <ripple/app/ledger/PendingSaves.h>
#include <ripple/app/ledger/InboundTransactions.h>
#include <ripple/app/ledger/TransactionMaster.h>
#include <ripple/app/main/LoadManager.h>
#include <ripple/app/main/NodeIdentity.h>
#include <ripple/app/main/NodeStoreScheduler.h>
#include <ripple/app/misc/AmendmentTable.h>
#include <ripple/app/misc/HashRouter.h>
#include <ripple/app/misc/LoadFeeTrack.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/misc/SHAMapStore.h>
#include <ripple/app/misc/TxQ.h>
#include <ripple/app/misc/ValidatorSite.h>
#include <ripple/app/misc/ValidatorKeys.h>
#include <ripple/app/paths/PathRequests.h>
#include <ripple/app/tx/apply.h>
#include <ripple/basics/ResolverAsio.h>
#include <ripple/basics/Sustain.h>
#include <ripple/basics/PerfLog.h>
#include <ripple/json/json_reader.h>
#include <ripple/nodestore/DummyScheduler.h>
#include <ripple/overlay/Cluster.h>
#include <ripple/overlay/make_Overlay.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/STParsedJSON.h>
#include <ripple/protocol/Protocol.h>
#include <ripple/resource/Fees.h>
#include <ripple/beast/asio/io_latency_probe.h>
#include <ripple/beast/core/LexicalCast.h>
#include <boost/asio/steady_timer.hpp>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>

namespace ripple {

// 204/256 about 80%
static int const MAJORITY_FRACTION (204);

//------------------------------------------------------------------------------

namespace detail {

class AppFamily : public Family
{
private:
    Application& app_;
    TreeNodeCache treecache_;
    FullBelowCache fullbelow_;
    NodeStore::Database& db_;
    bool const shardBacked_;
    beast::Journal j_;

    // missing node handler
    LedgerIndex maxSeq = 0;
    std::mutex maxSeqLock;

    void acquire (
        uint256 const& hash,
        std::uint32_t seq)
    {
        if (hash.isNonZero())
        {
            auto j = app_.journal ("Ledger");

            JLOG (j.error()) <<
                "Missing node in " << to_string (hash);

            app_.getInboundLedgers ().acquire (
                hash, seq, shardBacked_ ?
                InboundLedger::Reason::SHARD :
                InboundLedger::Reason::GENERIC);
        }
    }

public:
    AppFamily (AppFamily const&) = delete;
    AppFamily& operator= (AppFamily const&) = delete;

    AppFamily (Application& app, NodeStore::Database& db,
            CollectorManager& collectorManager)
        : app_ (app)
        , treecache_ ("TreeNodeCache", 65536, 60, stopwatch(),
            app.journal("TaggedCache"))
        , fullbelow_ ("full_below", stopwatch(),
            collectorManager.collector(),
                fullBelowTargetSize, fullBelowExpirationSeconds)
        , db_ (db)
        , shardBacked_ (
            dynamic_cast<NodeStore::DatabaseShard*>(&db) != nullptr)
        , j_ (app.journal("SHAMap"))
    {
    }

    beast::Journal const&
    journal() override
    {
        return j_;
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

    bool
    isShardBacked() const override
    {
        return shardBacked_;
    }

    void
    missing_node (std::uint32_t seq) override
    {
        auto j = app_.journal ("Ledger");

        JLOG (j.error()) <<
            "Missing node in " << seq;

        // prevent recursive invocation
        std::unique_lock <std::mutex> lock (maxSeqLock);

        if (maxSeq == 0)
        {
            maxSeq = seq;

            do
            {
                // Try to acquire the most recent missing ledger
                seq = maxSeq;

                lock.unlock();

                // This can invoke the missing node handler
                acquire (
                    app_.getLedgerMaster().getHashBySeq (seq),
                    seq);

                lock.lock();
            }
            while (maxSeq != seq);
        }
        else if (maxSeq < seq)
        {
            // We found a more recent ledger with a
            // missing node
            maxSeq = seq;
        }
    }

    void
    missing_node (uint256 const& hash, std::uint32_t seq) override
    {
        acquire (hash, seq);
    }

    void
    reset () override
    {
        {
            std::lock_guard<std::mutex> l(maxSeqLock);
            maxSeq = 0;
        }
        fullbelow_.reset();
        treecache_.reset();
    }
};

} // detail

//------------------------------------------------------------------------------

// VFALCO TODO Move the function definitions into the class declaration
class ApplicationImp
    : public Application
    , public RootStoppable
    , public BasicApp
{
private:
    class io_latency_sampler
    {
    private:
        beast::insight::Event m_event;
        beast::Journal m_journal;
        beast::io_latency_probe <std::chrono::steady_clock> m_probe;
        std::atomic<std::chrono::milliseconds> lastSample_;

    public:
        io_latency_sampler (
            beast::insight::Event ev,
            beast::Journal journal,
            std::chrono::milliseconds interval,
            boost::asio::io_service& ios)
            : m_event (ev)
            , m_journal (journal)
            , m_probe (interval, ios)
            , lastSample_ {}
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
            using namespace std::chrono;
            auto const lastSample = date::ceil<milliseconds>(elapsed);

            lastSample_ = lastSample;

            if (lastSample >= 10ms)
                m_event.notify (lastSample);
            if (lastSample >= 500ms)
            {
                JLOG(m_journal.warn()) <<
                    "io_service latency = " << lastSample.count();
            }
        }

        std::chrono::milliseconds
        get () const
        {
            return lastSample_.load();
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
    std::unique_ptr<Config> config_;
    std::unique_ptr<Logs> logs_;
    std::unique_ptr<TimeKeeper> timeKeeper_;

    beast::Journal m_journal;
    std::unique_ptr<perf::PerfLog> perfLog_;
    Application::MutexType m_masterMutex;

    // Required by the SHAMapStore
    TransactionMaster m_txMaster;

    NodeStoreScheduler m_nodeStoreScheduler;
    std::unique_ptr <SHAMapStore> m_shaMapStore;
    PendingSaves pendingSaves_;
    AccountIDCache accountIDCache_;
    boost::optional<OpenLedger> openLedger_;

    // These are not Stoppable-derived
    NodeCache m_tempNodeCache;
    std::unique_ptr <CollectorManager> m_collectorManager;
    CachedSLEs cachedSLEs_;
    std::pair<PublicKey, SecretKey> nodeIdentity_;
    ValidatorKeys const validatorKeys_;

    std::unique_ptr <Resource::Manager> m_resourceManager;

    // These are Stoppable-related
    std::unique_ptr <JobQueue> m_jobQueue;
    std::unique_ptr <NodeStore::Database> m_nodeStore;
    std::unique_ptr <NodeStore::DatabaseShard> shardStore_;
    detail::AppFamily family_;
    std::unique_ptr <detail::AppFamily> sFamily_;
    // VFALCO TODO Make OrderBookDB abstract
    OrderBookDB m_orderBookDB;
    std::unique_ptr <PathRequests> m_pathRequests;
    std::unique_ptr <LedgerMaster> m_ledgerMaster;
    std::unique_ptr <InboundLedgers> m_inboundLedgers;
    std::unique_ptr <InboundTransactions> m_inboundTransactions;
    TaggedCache <uint256, AcceptedLedger> m_acceptedLedgerCache;
    std::unique_ptr <NetworkOPs> m_networkOPs;
    std::unique_ptr <Cluster> cluster_;
    std::unique_ptr <ManifestCache> validatorManifests_;
    std::unique_ptr <ManifestCache> publisherManifests_;
    std::unique_ptr <ValidatorList> validators_;
    std::unique_ptr <ValidatorSite> validatorSites_;
    std::unique_ptr <ServerHandler> serverHandler_;
    std::unique_ptr <AmendmentTable> m_amendmentTable;
    std::unique_ptr <LoadFeeTrack> mFeeTrack;
    std::unique_ptr <HashRouter> mHashRouter;
    RCLValidations mValidations;
    std::unique_ptr <LoadManager> m_loadManager;
    std::unique_ptr <TxQ> txQ_;
    ClosureCounter<void, boost::system::error_code const&> waitHandlerCounter_;
    boost::asio::steady_timer sweepTimer_;
    boost::asio::steady_timer entropyTimer_;
    bool startTimers_;

    std::unique_ptr <DatabaseCon> mTxnDB;
    std::unique_ptr <DatabaseCon> mLedgerDB;
    std::unique_ptr <DatabaseCon> mWalletDB;
    std::unique_ptr <Overlay> m_overlay;
    std::vector <std::unique_ptr<Stoppable>> websocketServers_;

    boost::asio::signal_set m_signals;
    beast::WaitableEvent m_stop;

    std::atomic<bool> checkSigs_;

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

    ApplicationImp (
            std::unique_ptr<Config> config,
            std::unique_ptr<Logs> logs,
            std::unique_ptr<TimeKeeper> timeKeeper)
        : RootStoppable ("Application")
        , BasicApp (numberOfThreads(*config))
        , config_ (std::move(config))
        , logs_ (std::move(logs))
        , timeKeeper_ (std::move(timeKeeper))

        , m_journal (logs_->journal("Application"))

        // PerfLog must be started before any other threads are launched.
        , perfLog_ (perf::make_PerfLog(
            perf::setup_PerfLog(config_->section("perf"), config_->CONFIG_DIR),
            *this, logs_->journal("PerfLog"), [this] () { signalStop(); }))

        , m_txMaster (*this)

        , m_nodeStoreScheduler (*this)

        , m_shaMapStore (make_SHAMapStore (*this, setup_SHAMapStore (*config_),
            *this, m_nodeStoreScheduler, logs_->journal("SHAMapStore"),
            logs_->journal("NodeObject"), m_txMaster, *config_))

        , accountIDCache_(128000)

        , m_tempNodeCache ("NodeCache", 16384, 90, stopwatch(),
            logs_->journal("TaggedCache"))

        , m_collectorManager (CollectorManager::New (
            config_->section (SECTION_INSIGHT), logs_->journal("Collector")))
        , cachedSLEs_ (std::chrono::minutes(1), stopwatch())
        , validatorKeys_(*config_, m_journal)

        , m_resourceManager (Resource::make_Manager (
            m_collectorManager->collector(), logs_->journal("Resource")))

        // The JobQueue has to come pretty early since
        // almost everything is a Stoppable child of the JobQueue.
        //
        , m_jobQueue (std::make_unique<JobQueue>(
            m_collectorManager->group ("jobq"), m_nodeStoreScheduler,
            logs_->journal("JobQueue"), *logs_, *perfLog_))

        //
        // Anything which calls addJob must be a descendant of the JobQueue
        //
        , m_nodeStore (
            m_shaMapStore->makeDatabase ("NodeStore.main", 4, *m_jobQueue))

        , shardStore_ (
            m_shaMapStore->makeDatabaseShard ("ShardStore", 4, *m_jobQueue))

        , family_ (*this, *m_nodeStore, *m_collectorManager)

        , m_orderBookDB (*this, *m_jobQueue)

        , m_pathRequests (std::make_unique<PathRequests> (
            *this, logs_->journal("PathRequest"), m_collectorManager->collector ()))

        , m_ledgerMaster (std::make_unique<LedgerMaster> (*this, stopwatch (),
            *m_jobQueue, m_collectorManager->collector (),
            logs_->journal("LedgerMaster")))

        // VFALCO NOTE must come before NetworkOPs to prevent a crash due
        //             to dependencies in the destructor.
        //
        , m_inboundLedgers (make_InboundLedgers (*this, stopwatch(),
            *m_jobQueue, m_collectorManager->collector ()))

        , m_inboundTransactions (make_InboundTransactions
            ( *this, stopwatch()
            , *m_jobQueue
            , m_collectorManager->collector ()
            , [this](std::shared_ptr <SHAMap> const& set,
                bool fromAcquire)
            {
                gotTXSet (set, fromAcquire);
            }))

        , m_acceptedLedgerCache ("AcceptedLedger", 4, 60, stopwatch(),
            logs_->journal("TaggedCache"))

        , m_networkOPs (make_NetworkOPs (*this, stopwatch(),
            config_->standalone(), config_->NETWORK_QUORUM, config_->START_VALID,
            *m_jobQueue, *m_ledgerMaster, *m_jobQueue, validatorKeys_,
            get_io_service(), logs_->journal("NetworkOPs")))

        , cluster_ (std::make_unique<Cluster> (
            logs_->journal("Overlay")))

        , validatorManifests_ (std::make_unique<ManifestCache> (
            logs_->journal("ManifestCache")))

        , publisherManifests_ (std::make_unique<ManifestCache> (
            logs_->journal("ManifestCache")))

        , validators_ (std::make_unique<ValidatorList> (
            *validatorManifests_, *publisherManifests_, *timeKeeper_,
            logs_->journal("ValidatorList"), config_->VALIDATION_QUORUM))

        , validatorSites_ (std::make_unique<ValidatorSite> (
            get_io_service (), *validators_, logs_->journal("ValidatorSite")))

        , serverHandler_ (make_ServerHandler (*this, *m_networkOPs, get_io_service (),
            *m_jobQueue, *m_networkOPs, *m_resourceManager,
            *m_collectorManager))

        , mFeeTrack (std::make_unique<LoadFeeTrack>(logs_->journal("LoadManager")))

        , mHashRouter (std::make_unique<HashRouter>(
            stopwatch(), HashRouter::getDefaultHoldTime (),
            HashRouter::getDefaultRecoverLimit ()))

        , mValidations (ValidationParms(),stopwatch(), *this, logs_->journal("Validations"))

        , m_loadManager (make_LoadManager (*this, *this, logs_->journal("LoadManager")))

        , txQ_(make_TxQ(setup_TxQ(*config_), logs_->journal("TxQ")))

        , sweepTimer_ (get_io_service())

        , entropyTimer_ (get_io_service())

        , startTimers_ (false)

        , m_signals (get_io_service())

        , checkSigs_(true)

        , m_resolver (ResolverAsio::New (get_io_service(), logs_->journal("Resolver")))

        , m_io_latency_sampler (m_collectorManager->collector()->make_event ("ios_latency"),
            logs_->journal("Application"), std::chrono::milliseconds (100), get_io_service())
    {
        if (shardStore_)
            sFamily_ = std::make_unique<detail::AppFamily>(
                *this, *shardStore_, *m_collectorManager);
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
    }

    //--------------------------------------------------------------------------

    bool setup() override;
    void doStart(bool withTimers) override;
    void run() override;
    bool isShutdown() override;
    void signalStop() override;
    bool checkSigs() const override;
    void checkSigs(bool) override;
    int fdlimit () const override;

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

    CollectorManager& getCollectorManager () override
    {
        return *m_collectorManager;
    }

    Family& family() override
    {
        return family_;
    }

    Family* shardFamily() override
    {
        return sFamily_.get();
    }

    TimeKeeper&
    timeKeeper() override
    {
        return *timeKeeper_;
    }

    JobQueue& getJobQueue () override
    {
        return *m_jobQueue;
    }

    std::pair<PublicKey, SecretKey> const&
    nodeIdentity () override
    {
        return nodeIdentity_;
    }

    
    PublicKey const &
    getValidationPublicKey() const override
    {
        return validatorKeys_.publicKey;
    }

    NetworkOPs& getOPs () override
    {
        return *m_networkOPs;
    }

    boost::asio::io_service& getIOService () override
    {
        return get_io_service();
    }

    std::chrono::milliseconds getIOLatency () override
    {
        return m_io_latency_sampler.get ();
    }

    LedgerMaster& getLedgerMaster () override
    {
        return *m_ledgerMaster;
    }

    InboundLedgers& getInboundLedgers () override
    {
        return *m_inboundLedgers;
    }

    InboundTransactions& getInboundTransactions () override
    {
        return *m_inboundTransactions;
    }

    TaggedCache <uint256, AcceptedLedger>& getAcceptedLedgerCache () override
    {
        return m_acceptedLedgerCache;
    }

    void gotTXSet (std::shared_ptr<SHAMap> const& set, bool fromAcquire)
    {
        if (set)
            m_networkOPs->mapComplete (set, fromAcquire);
    }

    TransactionMaster& getMasterTransaction () override
    {
        return m_txMaster;
    }

    perf::PerfLog& getPerfLog () override
    {
        return *perfLog_;
    }

    NodeCache& getTempNodeCache () override
    {
        return m_tempNodeCache;
    }

    NodeStore::Database& getNodeStore () override
    {
        return *m_nodeStore;
    }

    NodeStore::DatabaseShard* getShardStore () override
    {
        return shardStore_.get();
    }

    Application::MutexType& getMasterMutex () override
    {
        return m_masterMutex;
    }

    LoadManager& getLoadManager () override
    {
        return *m_loadManager;
    }

    Resource::Manager& getResourceManager () override
    {
        return *m_resourceManager;
    }

    OrderBookDB& getOrderBookDB () override
    {
        return m_orderBookDB;
    }

    PathRequests& getPathRequests () override
    {
        return *m_pathRequests;
    }

    CachedSLEs&
    cachedSLEs() override
    {
        return cachedSLEs_;
    }

    AmendmentTable& getAmendmentTable() override
    {
        return *m_amendmentTable;
    }

    LoadFeeTrack& getFeeTrack () override
    {
        return *mFeeTrack;
    }

    HashRouter& getHashRouter () override
    {
        return *mHashRouter;
    }

    RCLValidations& getValidations () override
    {
        return mValidations;
    }

    ValidatorList& validators () override
    {
        return *validators_;
    }

    ValidatorSite& validatorSites () override
    {
        return *validatorSites_;
    }

    ManifestCache& validatorManifests() override
    {
        return *validatorManifests_;
    }

    ManifestCache& publisherManifests() override
    {
        return *publisherManifests_;
    }

    Cluster& cluster () override
    {
        return *cluster_;
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

    OpenLedger const&
    openLedger() const override
    {
        return *openLedger_;
    }

    Overlay& overlay () override
    {
        return *m_overlay;
    }

    TxQ& getTxQ() override
    {
        assert(txQ_.get() != nullptr);
        return *txQ_;
    }

    DatabaseCon& getTxnDB () override
    {
        assert (mTxnDB.get() != nullptr);
        return *mTxnDB;
    }
    DatabaseCon& getLedgerDB () override
    {
        assert (mLedgerDB.get() != nullptr);
        return *mLedgerDB;
    }
    DatabaseCon& getWalletDB () override
    {
        assert (mWalletDB.get() != nullptr);
        return *mWalletDB;
    }

    bool serverOkay (std::string& reason) override;

    beast::Journal journal (std::string const& name) override;

    //--------------------------------------------------------------------------
    bool initSqliteDbs ()
    {
        assert (mTxnDB.get () == nullptr);
        assert (mLedgerDB.get () == nullptr);
        assert (mWalletDB.get () == nullptr);

        DatabaseCon::Setup setup = setup_DatabaseCon (*config_);
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
            JLOG(m_journal.error()) << "Received signal: " << signal_number
                                  << " with error: " << ec.message();
        }
        else
        {
            JLOG(m_journal.debug()) << "Received signal: " << signal_number;
            signalStop();
        }
    }

    //--------------------------------------------------------------------------
    //
    // Stoppable
    //

    void onPrepare() override
    {
    }

    void onStart () override
    {
        JLOG(m_journal.info())
            << "Application starting. Version is " << BuildInfo::getVersionString();

        using namespace std::chrono_literals;
        if(startTimers_)
        {
            setSweepTimer();
            setEntropyTimer();
        }

        m_io_latency_sampler.start();

        m_resolver->start ();
    }

    // Called to indicate shutdown.
    void onStop () override
    {
        JLOG(m_journal.debug()) << "Application stopping";

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

        {
            boost::system::error_code ec;
            sweepTimer_.cancel (ec);
            if (ec)
            {
                JLOG (m_journal.error())
                    << "Application: sweepTimer cancel error: "
                    << ec.message();
            }

            ec.clear();
            entropyTimer_.cancel (ec);
            if (ec)
            {
                JLOG (m_journal.error())
                    << "Application: entropyTimer cancel error: "
                    << ec.message();
            }
        }
        // Make sure that any waitHandlers pending in our timers are done
        // before we declare ourselves stopped.
        waitHandlerCounter_.join("Application", 1s, m_journal);

        JLOG(m_journal.debug()) << "Flushing validations";
        mValidations.flush ();
        JLOG(m_journal.debug()) << "Validations flushed";

        validatorSites_->stop ();

        // TODO Store manifests in manifests.sqlite instead of wallet.db
        validatorManifests_->save (getWalletDB (), "ValidatorManifests",
            [this](PublicKey const& pubKey)
            {
                return validators().listed (pubKey);
            });

        publisherManifests_->save (getWalletDB (), "PublisherManifests",
            [this](PublicKey const& pubKey)
            {
                return validators().trustedPublisher (pubKey);
            });

        stopped ();
    }

    //--------------------------------------------------------------------------
    //
    // PropertyStream
    //

    void onWrite (beast::PropertyStream::Map& stream) override
    {
    }

    //--------------------------------------------------------------------------

    void setSweepTimer ()
    {
        // Only start the timer if waitHandlerCounter_ is not yet joined.
        if (auto optionalCountedHandler = waitHandlerCounter_.wrap (
            [this] (boost::system::error_code const& e)
            {
                if ((e.value() == boost::system::errc::success) &&
                    (! m_jobQueue->isStopped()))
                {
                    m_jobQueue->addJob(
                        jtSWEEP, "sweep", [this] (Job&) { doSweep(); });
                }
                // Recover as best we can if an unexpected error occurs.
                if (e.value() != boost::system::errc::success &&
                    e.value() != boost::asio::error::operation_aborted)
                {
                    // Try again later and hope for the best.
                    JLOG (m_journal.error())
                       << "Sweep timer got error '" << e.message()
                       << "'.  Restarting timer.";
                    setSweepTimer();
                }
            }))
        {
            sweepTimer_.expires_from_now (
                std::chrono::seconds {config_->getSize (siSweepInterval)});
            sweepTimer_.async_wait (std::move (*optionalCountedHandler));
        }
    }

    void setEntropyTimer ()
    {
        // Only start the timer if waitHandlerCounter_ is not yet joined.
        if (auto optionalCountedHandler = waitHandlerCounter_.wrap (
            [this] (boost::system::error_code const& e)
            {
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
                    JLOG (m_journal.error())
                       << "Entropy timer got error '" << e.message()
                       << "'.  Restarting timer.";
                    setEntropyTimer();
                }
            }))
        {
            using namespace std::chrono_literals;
            entropyTimer_.expires_from_now (5min);
            entropyTimer_.async_wait (std::move (*optionalCountedHandler));
        }
    }

    void doSweep ()
    {
        if (! config_->standalone())
        {
            boost::filesystem::space_info space =
                boost::filesystem::space (config_->legacy ("database_path"));

            constexpr std::uintmax_t bytes512M = 512 * 1024 * 1024;
            if (space.available < (bytes512M))
            {
                JLOG(m_journal.fatal())
                    << "Remaining free disk space is less than 512MB";
                signalStop ();
            }
        }

        // VFALCO NOTE Does the order of calls matter?
        // VFALCO TODO fix the dependency inversion using an observer,
        //         have listeners register for "onSweep ()" notification.

        family().fullbelow().sweep();
        if (sFamily_)
            sFamily_->fullbelow().sweep();
        getMasterTransaction().sweep();
        getNodeStore().sweep();
        if (shardStore_)
            shardStore_->sweep();
        getLedgerMaster().sweep();
        getTempNodeCache().sweep();
        getValidations().expire();
        getInboundLedgers().sweep();
        m_acceptedLedgerCache.sweep();
        family().treecache().sweep();
        if (sFamily_)
            sFamily_->treecache().sweep();
        cachedSLEs_.expire();

        // Set timer to do another sweep later.
        setSweepTimer();
    }

    LedgerIndex getMaxDisallowedLedger() override
    {
        return maxDisallowedLedger_;
    }


private:
    // For a newly-started validator, this is the greatest persisted ledger
    // and new validations must be greater than this.
    std::atomic<LedgerIndex> maxDisallowedLedger_ {0};

    void addTxnSeqField();
    void addValidationSeqFields();
    bool updateTables ();
    bool nodeToShards ();
    bool validateShards ();
    void startGenesisLedger ();

    std::shared_ptr<Ledger>
    getLastFullLedger();

    std::shared_ptr<Ledger>
    loadLedgerFromFile (
        std::string const& ledgerID);

    bool loadOldLedger (
        std::string const& ledgerID,
        bool replay,
        bool isFilename);

    void setMaxDisallowedLedger();
};

//------------------------------------------------------------------------------

// VFALCO TODO Break this function up into many small initialization segments.
//             Or better yet refactor these initializations into RAII classes
//             which are members of the Application object.
//
bool ApplicationImp::setup()
{
    // VFALCO NOTE: 0 means use heuristics to determine the thread count.
    m_jobQueue->setThreadCount (config_->WORKERS, config_->standalone());

    // We want to intercept and wait for CTRL-C to terminate the process
    m_signals.add (SIGINT);

    m_signals.async_wait(std::bind(&ApplicationImp::signalled, this,
        std::placeholders::_1, std::placeholders::_2));

    assert (mTxnDB == nullptr);

    auto debug_log = config_->getDebugLogFile ();

    if (!debug_log.empty ())
    {
        // Let debug messages go to the file but only WARNING or higher to
        // regular output (unless verbose)

        if (!logs_->open(debug_log))
            std::cerr << "Can't open log file " << debug_log << '\n';

        using namespace beast::severities;
        if (logs_->threshold() > kDebug)
            logs_->threshold (kDebug);
    }

    logs_->silent (config_->silent());

    if (!config_->standalone())
        timeKeeper_->run(config_->SNTP_SERVERS);

    if (!initSqliteDbs ())
    {
        JLOG(m_journal.fatal()) << "Cannot create database connections!";
        return false;
    }

    if (validatorKeys_.publicKey.size())
        setMaxDisallowedLedger();

    getLedgerDB ().getSession ()
        << boost::str (boost::format ("PRAGMA cache_size=-%d;") %
                        (config_->getSize (siLgrDBCache) * 1024));

    getTxnDB ().getSession ()
            << boost::str (boost::format ("PRAGMA cache_size=-%d;") %
                            (config_->getSize (siTxnDBCache) * 1024));

    mTxnDB->setupCheckpointing (m_jobQueue.get(), logs());
    mLedgerDB->setupCheckpointing (m_jobQueue.get(), logs());

    if (!updateTables ())
        return false;

    // Configure the amendments the server supports
    {
        Section supportedAmendments ("Supported Amendments");
        supportedAmendments.append (detail::supportedAmendments ());

        Section enabledAmendments = config_->section (SECTION_AMENDMENTS);

        m_amendmentTable = make_AmendmentTable (
            weeks{2},
            MAJORITY_FRACTION,
            supportedAmendments,
            enabledAmendments,
            config_->section (SECTION_VETO_AMENDMENTS),
            logs_->journal("Amendments"));
    }

    Pathfinder::initPathTable();

    auto const startUp = config_->START_UP;
    if (startUp == Config::FRESH)
    {
        JLOG(m_journal.info()) << "Starting new Ledger";

        startGenesisLedger ();
    }
    else if (startUp == Config::LOAD ||
                startUp == Config::LOAD_FILE ||
                startUp == Config::REPLAY)
    {
        JLOG(m_journal.info()) <<
            "Loading specified Ledger";

        if (!loadOldLedger (config_->START_LEDGER,
                            startUp == Config::REPLAY,
                            startUp == Config::LOAD_FILE))
        {
            JLOG(m_journal.error()) <<
                "The specified ledger could not be loaded.";
            return false;
        }
    }
    else if (startUp == Config::NETWORK)
    {
        // This should probably become the default once we have a stable network.
        if (!config_->standalone())
            m_networkOPs->needNetworkLedger ();

        startGenesisLedger ();
    }
    else
    {
        startGenesisLedger ();
    }

    m_orderBookDB.setup (getLedgerMaster ().getCurrentLedger ());

    nodeIdentity_ = loadNodeIdentity (*this);

    if (!cluster_->load (config().section(SECTION_CLUSTER_NODES)))
    {
        JLOG(m_journal.fatal()) << "Invalid entry in cluster configuration.";
        return false;
    }

    {
        if(validatorKeys_.configInvalid())
            return false;

        if (!validatorManifests_->load (
            getWalletDB (), "ValidatorManifests", validatorKeys_.manifest,
            config().section (SECTION_VALIDATOR_KEY_REVOCATION).values ()))
        {
            JLOG(m_journal.fatal()) << "Invalid configured validator manifest.";
            return false;
        }

        publisherManifests_->load (
            getWalletDB (), "PublisherManifests");

        // Setup trusted validators
        if (!validators_->load (
                validatorKeys_.publicKey,
                config().section (SECTION_VALIDATORS).values (),
                config().section (SECTION_VALIDATOR_LIST_KEYS).values ()))
        {
            JLOG(m_journal.fatal()) <<
                "Invalid entry in validator configuration.";
            return false;
        }
    }

    if (!validatorSites_->load (
        config().section (SECTION_VALIDATOR_LIST_SITES).values ()))
    {
        JLOG(m_journal.fatal()) <<
            "Invalid entry in [" << SECTION_VALIDATOR_LIST_SITES << "]";
        return false;
    }

    m_nodeStore->tune (config_->getSize (siNodeCacheSize), config_->getSize (siNodeCacheAge));
    m_ledgerMaster->tune (config_->getSize (siLedgerSize), config_->getSize (siLedgerAge));
    family().treecache().setTargetSize (config_->getSize (siTreeCacheSize));
    family().treecache().setTargetAge (config_->getSize (siTreeCacheAge));
    if (shardStore_)
    {
        shardStore_->tune(config_->getSize(siNodeCacheSize),
            config_->getSize(siNodeCacheAge));
        sFamily_->treecache().setTargetSize(config_->getSize(siTreeCacheSize));
        sFamily_->treecache().setTargetAge(config_->getSize(siTreeCacheAge));
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
    m_overlay = make_Overlay (*this, setup_Overlay(*config_), *m_jobQueue,
        *serverHandler_, *m_resourceManager, *m_resolver, get_io_service(),
        *config_);
    add (*m_overlay); // add to PropertyStream

    if (!config_->standalone())
    {
        // validation and node import require the sqlite db
        if (config_->nodeToShard && !nodeToShards())
            return false;

        if (config_->validateShards && !validateShards())
            return false;
    }

    validatorSites_->start ();

    // start first consensus round
    if (! m_networkOPs->beginConsensus(m_ledgerMaster->getClosedLedger()->info().hash))
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
                if(std::strlen(e.what()) > 0)
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
        if (config_->PEER_PRIVATE && config_->IPS_FIXED.empty ())
        {
            JLOG(m_journal.warn())
                << "No outbound peer connections will be made";
        }

        // VFALCO NOTE the state timer resets the deadlock detector.
        //
        m_networkOPs->setStateTimer ();
    }
    else
    {
        JLOG(m_journal.warn()) << "Running in standalone mode";

        m_networkOPs->setStandAlone ();
    }

    //
    // Execute start up rpc commands.
    //
    for (auto cmd : config_->section(SECTION_RPC_STARTUP).lines())
    {
        Json::Reader jrReader;
        Json::Value jvCommand;

        if (! jrReader.parse (cmd, jvCommand))
        {
            JLOG(m_journal.fatal()) <<
                "Couldn't parse entry in [" << SECTION_RPC_STARTUP <<
                "]: '" << cmd;
        }

        if (!config_->quiet())
        {
            JLOG(m_journal.fatal()) << "Startup RPC: " << jvCommand << std::endl;
        }

        Resource::Charge loadType = Resource::feeReferenceRPC;
        Resource::Consumer c;
        RPC::Context context { journal ("RPCHandler"), jvCommand, *this,
            loadType, getOPs (), getLedgerMaster(), c, Role::ADMIN };

        Json::Value jvResult;
        RPC::doCommand (context, jvResult);

        if (!config_->quiet())
        {
            JLOG(m_journal.fatal()) << "Result: " << jvResult << std::endl;
        }
    }

    return true;
}

void
ApplicationImp::doStart(bool withTimers)
{
    startTimers_ = withTimers;
    prepare ();
    start ();
}

void
ApplicationImp::run()
{
    if (!config_->standalone())
    {
        // VFALCO NOTE This seems unnecessary. If we properly refactor the load
        //             manager then the deadlock detector can just always be "armed"
        //
        getLoadManager ().activateDeadlockDetector ();
    }

    m_stop.wait ();

    // Stop the server. When this returns, all
    // Stoppable objects should be stopped.
    JLOG(m_journal.info()) << "Received shutdown request";
    stop (m_journal);
    JLOG(m_journal.info()) << "Done.";
    StopSustain();
}

void
ApplicationImp::signalStop()
{
    // Unblock the main thread (which is sitting in run()).
    //
    m_stop.signal();
}

bool
ApplicationImp::isShutdown()
{
    // from Stoppable mixin
    return isStopped();
}

bool ApplicationImp::checkSigs() const
{
    return checkSigs_;
}

void ApplicationImp::checkSigs(bool check)
{
    checkSigs_ = check;
}

int ApplicationImp::fdlimit() const
{
    // Standard handles, config file, misc I/O etc:
    int needed = 128;

    // 1.5 times the configured peer limit for peer connections:
    needed += static_cast<int>(0.5 + (1.5 * m_overlay->limit()));

    // the number of fds needed by the backend (internally
    // doubled if online delete is enabled).
    needed += std::max(5, m_shaMapStore->fdlimit());

    // One fd per incoming connection a port can accept, or
    // if no limit is set, assume it'll handle 256 clients.
    for(auto const& p : serverHandler_->setup().ports)
        needed += std::max (256, p.limit);

    // The minimum number of file descriptors we need is 1024:
    return std::max(1024, needed);
}

//------------------------------------------------------------------------------

void
ApplicationImp::startGenesisLedger()
{
    std::vector<uint256> initialAmendments =
        (config_->START_UP == Config::FRESH) ?
            m_amendmentTable->getDesired() :
            std::vector<uint256>{};

    std::shared_ptr<Ledger> const genesis =
        std::make_shared<Ledger>(
            create_genesis,
            *config_,
            initialAmendments,
            family());
    m_ledgerMaster->storeLedger (genesis);

    auto const next = std::make_shared<Ledger>(
        *genesis, timeKeeper().closeTime());
    next->updateSkipList ();
    next->setImmutable (*config_);
    openLedger_.emplace(next, cachedSLEs_,
        logs_->journal("OpenLedger"));
    m_ledgerMaster->storeLedger(next);
    m_ledgerMaster->switchLCL (next);
}

std::shared_ptr<Ledger>
ApplicationImp::getLastFullLedger()
{
    auto j = journal ("Ledger");

    try
    {
        std::shared_ptr<Ledger> ledger;
        std::uint32_t seq;
        uint256 hash;

        std::tie (ledger, seq, hash) =
            loadLedgerHelper (
                "order by LedgerSeq desc limit 1", *this);

        if (!ledger)
            return ledger;

        ledger->setImmutable(*config_);

        if (getLedgerMaster ().haveLedger (seq))
            ledger->setValidated ();

        if (ledger->info().hash == hash)
        {
            JLOG (j.trace()) << "Loaded ledger: " << hash;
            return ledger;
        }

        if (auto stream = j.error())
        {
            stream  << "Failed on ledger";
            Json::Value p;
            addJson (p, {*ledger, LedgerFill::full});
            stream << p;
        }

        return {};
    }
    catch (SHAMapMissingNode& sn)
    {
        JLOG (j.warn()) <<
            "Ledger with missing nodes in database: " << sn;
        return {};
    }
}

std::shared_ptr<Ledger>
ApplicationImp::loadLedgerFromFile (
    std::string const& name)
{
    try
    {
        std::ifstream ledgerFile (name, std::ios::in);

        if (!ledgerFile)
        {
            JLOG(m_journal.fatal()) <<
                "Unable to open file '" << name << "'";
            return nullptr;
        }

        Json::Reader reader;
        Json::Value jLedger;

        if (!reader.parse (ledgerFile, jLedger))
        {
            JLOG(m_journal.fatal()) <<
                "Unable to parse ledger JSON";
            return nullptr;
        }

        std::reference_wrapper<Json::Value> ledger (jLedger);

         // accept a wrapped ledger
         if (ledger.get().isMember  ("result"))
            ledger = ledger.get()["result"];

         if (ledger.get().isMember ("ledger"))
            ledger = ledger.get()["ledger"];

        std::uint32_t seq = 1;
        auto closeTime = timeKeeper().closeTime();
        using namespace std::chrono_literals;
        auto closeTimeResolution = 30s;
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
                using tp = NetClock::time_point;
                using d = tp::duration;
                closeTime = tp{d{ledger.get()["close_time"].asUInt()}};
            }
            if (ledger.get().isMember ("close_time_resolution"))
            {
                closeTimeResolution = std::chrono::seconds{
                    ledger.get()["close_time_resolution"].asUInt()};
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

        if (!ledger.get().isArrayOrNull ())
        {
            JLOG(m_journal.fatal())
               << "State nodes must be an array";
            return nullptr;
        }

        auto loadLedger = std::make_shared<Ledger> (
            seq, closeTime, *config_, family());
        loadLedger->setTotalDrops(totalDrops);

        for (Json::UInt index = 0; index < ledger.get().size(); ++index)
        {
            Json::Value& entry = ledger.get()[index];

            if (!entry.isObjectOrNull())
            {
                JLOG(m_journal.fatal())
                    << "Invalid entry in ledger";
                return nullptr;
            }

            uint256 uIndex;

            if (!uIndex.SetHex (entry[jss::index].asString()))
            {
                JLOG(m_journal.fatal())
                    << "Invalid entry in ledger";
                return nullptr;
            }

            entry.removeMember (jss::index);

            STParsedJSONObject stp ("sle", ledger.get()[index]);

            if (!stp.object || uIndex.isZero ())
            {
                JLOG(m_journal.fatal())
                   << "Invalid entry in ledger";
                return nullptr;
            }

            // VFALCO TODO This is the only place that
            //             constructor is used, try to remove it
            STLedgerEntry sle (*stp.object, uIndex);

            if (! loadLedger->addSLE (sle))
            {
                JLOG(m_journal.fatal())
                   << "Couldn't add serialized ledger: "
                   << uIndex;
                return nullptr;
            }
        }

        loadLedger->stateMap().flushDirty (
            hotACCOUNT_NODE, loadLedger->info().seq);

        loadLedger->setAccepted (closeTime,
            closeTimeResolution, ! closeTimeEstimated,
               *config_);

        return loadLedger;
    }
    catch (std::exception const& x)
    {
        JLOG (m_journal.fatal()) <<
            "Ledger contains invalid data: " << x.what();
        return nullptr;
    }
}

bool ApplicationImp::loadOldLedger (
    std::string const& ledgerID, bool replay, bool isFileName)
{
    try
    {
        std::shared_ptr<Ledger const> loadLedger, replayLedger;

        if (isFileName)
        {
            if (!ledgerID.empty())
                loadLedger = loadLedgerFromFile (ledgerID);
        }
        else if (ledgerID.length () == 64)
        {
            uint256 hash;

            if (hash.SetHex (ledgerID))
            {
                loadLedger = loadByHash (hash, *this);

                if (!loadLedger)
                {
                    // Try to build the ledger from the back end
                    auto il = std::make_shared <InboundLedger> (
                        *this, hash, 0, InboundLedger::Reason::GENERIC,
                        stopwatch());
                    if (il->checkLocal ())
                        loadLedger = il->getLedger ();
                }
            }
        }
        else if (ledgerID.empty () || beast::detail::iequals(ledgerID, "latest"))
        {
            loadLedger = getLastFullLedger ();
        }
        else
        {
            // assume by sequence
            std::uint32_t index;

            if (beast::lexicalCastChecked (index, ledgerID))
                loadLedger = loadByIndex (index, *this);
        }

        if (!loadLedger)
            return false;

        if (replay)
        {
            // Replay a ledger close with same prior ledger and transactions

            // this ledger holds the transactions we want to replay
            replayLedger = loadLedger;

            JLOG(m_journal.info()) << "Loading parent ledger";

            loadLedger = loadByHash (replayLedger->info().parentHash, *this);
            if (!loadLedger)
            {
                JLOG(m_journal.info()) << "Loading parent ledger from node store";

                // Try to build the ledger from the back end
                auto il = std::make_shared <InboundLedger> (
                    *this, replayLedger->info().parentHash,
                    0, InboundLedger::Reason::GENERIC, stopwatch());

                if (il->checkLocal ())
                    loadLedger = il->getLedger ();

                if (!loadLedger)
                {
                    JLOG(m_journal.fatal()) << "Replay ledger missing/damaged";
                    assert (false);
                    return false;
                }
            }
        }

        JLOG(m_journal.info()) <<
            "Loading ledger " << loadLedger->info().hash <<
            " seq:" << loadLedger->info().seq;

        if (loadLedger->info().accountHash.isZero ())
        {
            JLOG(m_journal.fatal()) << "Ledger is empty.";
            assert (false);
            return false;
        }

        if (!loadLedger->walkLedger (journal ("Ledger")))
        {
            JLOG(m_journal.fatal()) << "Ledger is missing nodes.";
            assert(false);
            return false;
        }

        if (!loadLedger->assertSane (journal ("Ledger")))
        {
            JLOG(m_journal.fatal()) << "Ledger is not sane.";
            assert(false);
            return false;
        }

        m_ledgerMaster->setLedgerRangePresent (
            loadLedger->info().seq,
            loadLedger->info().seq);

        m_ledgerMaster->switchLCL (loadLedger);
        loadLedger->setValidated();
        m_ledgerMaster->setFullLedger(loadLedger, true, false);
        openLedger_.emplace(loadLedger, cachedSLEs_,
            logs_->journal("OpenLedger"));

        if (replay)
        {
            // inject transaction(s) from the replayLedger into our open ledger
            // and build replay structure
            auto const& txns = replayLedger->txMap();
            auto replayData = std::make_unique <LedgerReplay> ();

            replayData->prevLedger_ = replayLedger;
            replayData->closeTime_ = replayLedger->info().closeTime;
            replayData->closeFlags_ = replayLedger->info().closeFlags;

            for (auto const& item : txns)
            {
                auto txID = item.key();
                auto txPair = replayLedger->txRead (txID);
                auto txIndex = (*txPair.second)[sfTransactionIndex];

                auto s = std::make_shared <Serializer> ();
                txPair.first->add(*s);

                forceValidity(getHashRouter(),
                    txID, Validity::SigGoodOnly);

                replayData->txns_.emplace (txIndex, txPair.first);

                openLedger_->modify(
                    [&txID, &s](OpenView& view, beast::Journal j)
                    {
                        view.rawTxInsert (txID, std::move (s), nullptr);
                        return true;
                    });
            }

            m_ledgerMaster->takeReplay (std::move (replayData));
        }
    }
    catch (SHAMapMissingNode&)
    {
        JLOG(m_journal.fatal()) <<
            "Data is missing for selected ledger";
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

beast::Journal
ApplicationImp::journal (std::string const& name)
{
    return logs_->journal (name);
}

//VFALCO TODO clean this up since it is just a file holding a single member function definition

static
std::vector<std::string>
getSchema (DatabaseCon& dbc, std::string const& dbName)
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

static bool schemaHas (
    DatabaseCon& dbc, std::string const& dbName, int line,
    std::string const& content, beast::Journal j)
{
    std::vector<std::string> schema = getSchema (dbc, dbName);

    if (static_cast<int> (schema.size ()) <= line)
    {
        JLOG (j.fatal()) << "Schema for " << dbName << " has too few lines";
        Throw<std::runtime_error> ("bad schema");
    }

    return schema[line].find (content) != std::string::npos;
}

void ApplicationImp::addTxnSeqField ()
{
    if (schemaHas (getTxnDB (), "AccountTransactions", 0, "TxnSeq", m_journal))
        return;

    JLOG (m_journal.warn()) << "Transaction sequence field is missing";

    auto& session = getTxnDB ().getSession ();

    std::vector< std::pair<uint256, int> > txIDs;
    txIDs.reserve (300000);

    JLOG (m_journal.info()) << "Parsing transactions";
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
            JLOG (m_journal.info()) << "No metadata for " << transID;
        }
        else
        {
            TxMeta m (transID, 0, txnMeta, journal ("TxMeta"));
            txIDs.push_back (std::make_pair (transID, m.getIndex ()));
        }

        if ((++i % 1000) == 0)
        {
            JLOG (m_journal.info()) << i << " transactions read";
        }
    }

    JLOG (m_journal.info()) << "All " << i << " transactions read";

    soci::transaction tr(session);

    JLOG (m_journal.info()) << "Dropping old index";
    session << "DROP INDEX AcctTxIndex;";

    JLOG (m_journal.info()) << "Altering table";
    session << "ALTER TABLE AccountTransactions ADD COLUMN TxnSeq INTEGER;";

    boost::format fmt ("UPDATE AccountTransactions SET TxnSeq = %d WHERE TransID = '%s';");
    i = 0;
    for (auto& t : txIDs)
    {
        session << boost::str (fmt % t.second % to_string (t.first));

        if ((++i % 1000) == 0)
        {
            JLOG (m_journal.info()) << i << " transactions updated";
        }
    }

    JLOG (m_journal.info()) << "Building new index";
    session << "CREATE INDEX AcctTxIndex ON AccountTransactions(Account, LedgerSeq, TxnSeq, TransID);";

    tr.commit ();
}

void ApplicationImp::addValidationSeqFields ()
{
    if (schemaHas(getLedgerDB(), "Validations", 0, "LedgerSeq", m_journal))
    {
        assert(schemaHas(getLedgerDB(), "Validations", 0, "InitialSeq", m_journal));
        return;
    }

    JLOG(m_journal.warn()) << "Validation sequence fields are missing";
    assert(!schemaHas(getLedgerDB(), "Validations", 0, "InitialSeq", m_journal));

    auto& session = getLedgerDB().getSession();

    soci::transaction tr(session);

    JLOG(m_journal.info()) << "Altering table";
    session << "ALTER TABLE Validations "
        "ADD COLUMN LedgerSeq       BIGINT UNSIGNED;";
    session << "ALTER TABLE Validations "
        "ADD COLUMN InitialSeq      BIGINT UNSIGNED;";

    // Create the indexes, too, so we don't have to
    // wait for the next startup, which may be a while.
    // These should be identical to those in LedgerDBInit
    JLOG(m_journal.info()) << "Building new indexes";
    session << "CREATE INDEX IF NOT EXISTS "
        "ValidationsBySeq ON Validations(LedgerSeq);";
    session << "CREATE INDEX IF NOT EXISTS ValidationsByInitialSeq "
        "ON Validations(InitialSeq, LedgerSeq);";

    tr.commit();
}

bool ApplicationImp::updateTables ()
{
    if (config_->section (ConfigSection::nodeDatabase ()).empty ())
    {
        JLOG (m_journal.fatal()) << "The [node_db] configuration setting has been updated and must be set";
        return false;
    }

    // perform any needed table updates
    assert (schemaHas (getTxnDB (), "AccountTransactions", 0, "TransID", m_journal));
    assert (!schemaHas (getTxnDB (), "AccountTransactions", 0, "foobar", m_journal));
    addTxnSeqField ();

    if (schemaHas (getTxnDB (), "AccountTransactions", 0, "PRIMARY", m_journal))
    {
        JLOG (m_journal.fatal()) << "AccountTransactions database should not have a primary key";
        return false;
    }

    addValidationSeqFields ();

    if (config_->doImport)
    {
        auto j = logs_->journal("NodeObject");
        NodeStore::DummyScheduler scheduler;
        std::unique_ptr <NodeStore::Database> source =
            NodeStore::Manager::instance().make_Database ("NodeStore.import",
                scheduler, 0, *m_jobQueue,
                config_->section(ConfigSection::importNodeDatabase ()), j);

        JLOG (j.warn())
            << "Node import from '" << source->getName () << "' to '"
            << getNodeStore ().getName () << "'.";

        getNodeStore().import (*source);
    }

    return true;
}

bool ApplicationImp::nodeToShards()
{
    assert(m_overlay);
    assert(!config_->standalone());

    if (config_->section(ConfigSection::shardDatabase()).empty())
    {
        JLOG (m_journal.fatal()) <<
            "The [shard_db] configuration setting must be set";
        return false;
    }
    if (!shardStore_)
    {
        JLOG(m_journal.fatal()) <<
            "Invalid [shard_db] configuration";
        return false;
    }
    shardStore_->importNodeStore();
    return true;
}

bool ApplicationImp::validateShards()
{
    assert(m_overlay);
    assert(!config_->standalone());

    if (config_->section(ConfigSection::shardDatabase()).empty())
    {
        JLOG (m_journal.fatal()) <<
            "The [shard_db] configuration setting must be set";
        return false;
    }
    if (!shardStore_)
    {
        JLOG(m_journal.fatal()) <<
            "Invalid [shard_db] configuration";
        return false;
    }
    shardStore_->validate();
    return true;
}

void ApplicationImp::setMaxDisallowedLedger()
{
    boost::optional <LedgerIndex> seq;
    {
        auto db = getLedgerDB().checkoutDb();
        *db << "SELECT MAX(LedgerSeq) FROM Ledgers;", soci::into(seq);
    }
    if (seq)
        maxDisallowedLedger_ = *seq;

    JLOG (m_journal.trace()) << "Max persisted ledger is "
                             << maxDisallowedLedger_;
}


//------------------------------------------------------------------------------

Application::Application ()
    : beast::PropertyStream::Source ("app")
{
}

//------------------------------------------------------------------------------

std::unique_ptr<Application>
make_Application (
    std::unique_ptr<Config> config,
    std::unique_ptr<Logs> logs,
    std::unique_ptr<TimeKeeper> timeKeeper)
{
    return std::make_unique<ApplicationImp> (
        std::move(config), std::move(logs),
            std::move(timeKeeper));
}

}
