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

#include <fstream>

#include <beast/asio/io_latency_probe.h>
#include <beast/module/core/thread/DeadlineTimer.h>
#include <ripple/basics/utility/Sustain.h>
#include <ripple/common/seconds_clock.h>
#include <ripple/module/app/main/Tuning.h>
#include <ripple/module/app/misc/ProofOfWorkFactory.h>
#include <ripple/module/rpc/Manager.h>
#include <ripple/nodestore/Database.h>
#include <ripple/nodestore/DummyScheduler.h>
#include <ripple/nodestore/Manager.h>
#include <ripple/overlay/make_Overlay.h>
#include <beast/asio/io_latency_probe.h>
#include <beast/module/core/thread/DeadlineTimer.h>
#include <fstream>
    
namespace ripple {

// VFALCO TODO Clean this global up
static bool volatile doShutdown = false;

static int const MAJORITY_FRACTION (204);

//------------------------------------------------------------------------------
//
// Specializations for LogPartition names

// VFALCO NOTE This is temporary, until I refactor LogPartition
//            and LogPartition::getJournal() to take a string
//
class ApplicationLog;
template <> char const* LogPartition::getPartitionName <ApplicationLog> () { return "Application"; }
class SiteFilesLog;
template <> char const* LogPartition::getPartitionName <SiteFilesLog> () { return "SiteFiles"; }
class ValidatorsLog;
template <> char const* LogPartition::getPartitionName <ValidatorsLog> () { return "Validators"; }
class JobQueueLog;
template <> char const* LogPartition::getPartitionName <JobQueueLog> () { return "JobQueue"; }
class NetworkOPsLog;
template <> char const* LogPartition::getPartitionName <NetworkOPsLog> () { return "NetworkOPs"; }
class RPCServiceManagerLog;
template <> char const* LogPartition::getPartitionName <RPCServiceManagerLog> () { return "RPCServiceManager"; }
class HTTPServerLog;
template <> char const* LogPartition::getPartitionName <HTTPServerLog> () { return "RPCServer"; }
class LoadManagerLog;
template <> char const* LogPartition::getPartitionName <LoadManagerLog> () { return "LoadManager"; }
class ResourceManagerLog;
template <> char const* LogPartition::getPartitionName <ResourceManagerLog> () { return "ResourceManager"; }
class PathRequestLog;
template <> char const* LogPartition::getPartitionName <PathRequestLog> () { return "PathRequest"; }
class RPCManagerLog;
template <> char const* LogPartition::getPartitionName <RPCManagerLog> () { return "RPCManager"; }
class AmendmentTableLog;
template <> char const* LogPartition::getPartitionName <AmendmentTableLog>() { return "AmendmentTable"; }

template <> char const* LogPartition::getPartitionName <CollectorManager> () { return "Collector"; }

struct TaggedCacheLog;
template <> char const* LogPartition::getPartitionName <TaggedCacheLog> () { return "TaggedCache"; }

//
//------------------------------------------------------------------------------

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

// VFALCO TODO Move the function definitions into the class declaration
class ApplicationImp
    : public ApplicationImpBase
    , public beast::RootStoppable
    , public beast::DeadlineTimer::Listener
    , public beast::LeakChecked <ApplicationImp>
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
    beast::Journal m_journal;
    Application::LockType m_masterMutex;

    // These are not Stoppable-derived
    std::unique_ptr <NodeStore::Manager> m_nodeStoreManager;

    NodeCache m_tempNodeCache;
    SLECache m_sleCache;
    LocalCredentials m_localCredentials;
    TransactionMaster m_txMaster;

    std::unique_ptr <CollectorManager> m_collectorManager;
    std::unique_ptr <Resource::Manager> m_resourceManager;
    std::unique_ptr <FullBelowCache> m_fullBelowCache;

    // These are Stoppable-related
    NodeStoreScheduler m_nodeStoreScheduler;
    std::unique_ptr <JobQueue> m_jobQueue;
    IoServicePool m_mainIoPool;
    std::unique_ptr <SiteFiles::Manager> m_siteFiles;
    std::unique_ptr <RPC::Manager> m_rpcManager;
    // VFALCO TODO Make OrderBookDB abstract
    OrderBookDB m_orderBookDB;
    std::unique_ptr <PathRequests> m_pathRequests;
    std::unique_ptr <LedgerMaster> m_ledgerMaster;
    std::unique_ptr <InboundLedgers> m_inboundLedgers;
    std::unique_ptr <NetworkOPs> m_networkOPs;
    std::unique_ptr <UniqueNodeList> m_deprecatedUNL;
    std::unique_ptr <RPCHTTPServer> m_rpcHTTPServer;
    RPCServerHandler m_rpcServerHandler;
    std::unique_ptr <NodeStore::Database> m_nodeStore;
    std::unique_ptr <SNTPClient> m_sntpClient;
    std::unique_ptr <TxQueue> m_txQueue;
    std::unique_ptr <Validators::Manager> m_validators;
    std::unique_ptr <AmendmentTable> m_amendmentTable;
    std::unique_ptr <LoadFeeTrack> mFeeTrack;
    std::unique_ptr <IHashRouter> mHashRouter;
    std::unique_ptr <Validations> mValidations;
    std::unique_ptr <ProofOfWorkFactory> mProofOfWorkFactory;
    std::unique_ptr <LoadManager> m_loadManager;
    beast::DeadlineTimer m_sweepTimer;
    bool volatile mShutdown;

    std::unique_ptr <DatabaseCon> mRpcDB;
    std::unique_ptr <DatabaseCon> mTxnDB;
    std::unique_ptr <DatabaseCon> mLedgerDB;
    std::unique_ptr <DatabaseCon> mWalletDB;

    std::unique_ptr <beast::asio::SSLContext> m_peerSSLContext;
    std::unique_ptr <beast::asio::SSLContext> m_wsSSLContext;
    std::unique_ptr <Overlay> m_peers;
    std::unique_ptr <RPCDoor>  m_rpcDoor;
    std::unique_ptr <WSDoor> m_wsPublicDoor;
    std::unique_ptr <WSDoor> m_wsPrivateDoor;
    std::unique_ptr <WSDoor> m_wsProxyDoor;

    beast::WaitableEvent m_stop;

    std::unique_ptr <ResolverAsio> m_resolver;

    io_latency_sampler m_io_latency_sampler;

    //--------------------------------------------------------------------------

    static std::vector <std::unique_ptr <NodeStore::Factory>> make_Factories ()
    {
        std::vector <std::unique_ptr <NodeStore::Factory>> list;

        // VFALCO NOTE SqliteFactory is here because it has
        //             dependencies like SqliteDatabase and DatabaseCon
        //
        list.emplace_back (make_SqliteFactory ());

        return list;
    }

    //--------------------------------------------------------------------------

    ApplicationImp ()
        : RootStoppable ("Application")
        , m_journal (LogPartition::getJournal <ApplicationLog> ())

        , m_nodeStoreManager (NodeStore::make_Manager (
            std::move (make_Factories ())))

        , m_tempNodeCache ("NodeCache", 16384, 90, get_seconds_clock (),
            LogPartition::getJournal <TaggedCacheLog> ())

        , m_sleCache ("LedgerEntryCache", 4096, 120, get_seconds_clock (),
            LogPartition::getJournal <TaggedCacheLog> ())

        , m_collectorManager (CollectorManager::New (
            getConfig().insightSettings,
                LogPartition::getJournal <CollectorManager> ()))

        , m_resourceManager (Resource::make_Manager (
            m_collectorManager->collector(),
                LogPartition::getJournal <ResourceManagerLog> ()))

        , m_fullBelowCache (std::make_unique <FullBelowCache> (
            "full_below", get_seconds_clock (), m_collectorManager->collector (),
                fullBelowTargetSize, fullBelowExpirationSeconds))

        , m_nodeStoreScheduler (*this)

        // The JobQueue has to come pretty early since
        // almost everything is a Stoppable child of the JobQueue.
        //
        , m_jobQueue (make_JobQueue (m_collectorManager->group ("jobq"),
            m_nodeStoreScheduler, LogPartition::getJournal <JobQueueLog> ()))

        // The io_service must be a child of the JobQueue since we call addJob
        // in response to newtwork data from peers and also client requests.
        //
        , m_mainIoPool (*m_jobQueue, "io", (getConfig ().NODE_SIZE >= 2) ? 2 : 1)

        //
        // Anything which calls addJob must be a descendant of the JobQueue
        //

        , m_siteFiles (SiteFiles::Manager::New (
            *this, LogPartition::getJournal <SiteFilesLog> ()))

        , m_rpcManager (RPC::make_Manager (LogPartition::getJournal <RPCManagerLog> ()))

        , m_orderBookDB (*m_jobQueue)

        , m_pathRequests ( new PathRequests (
            LogPartition::getJournal <PathRequestLog> (), m_collectorManager->collector ()))

        , m_ledgerMaster (LedgerMaster::New (
            *m_jobQueue, LogPartition::getJournal <LedgerMaster> ()))

        // VFALCO NOTE must come before NetworkOPs to prevent a crash due
        //             to dependencies in the destructor.
        //
        , m_inboundLedgers (InboundLedgers::New (get_seconds_clock (), *m_jobQueue,
                            m_collectorManager->collector ()))

        // VFALCO NOTE Does NetworkOPs depend on LedgerMaster?
        , m_networkOPs (NetworkOPs::New (get_seconds_clock (), *m_ledgerMaster,
            *m_jobQueue, LogPartition::getJournal <NetworkOPsLog> ()))

        // VFALCO NOTE LocalCredentials starts the deprecated UNL service
        , m_deprecatedUNL (UniqueNodeList::New (*m_jobQueue))

        , m_rpcHTTPServer (RPCHTTPServer::New (*m_networkOPs,
            LogPartition::getJournal <HTTPServerLog> (), *m_jobQueue, *m_networkOPs, *m_resourceManager))

        , m_rpcServerHandler (*m_networkOPs, *m_resourceManager) // passive object, not a Service

        , m_nodeStore (m_nodeStoreManager->make_Database ("NodeStore.main", m_nodeStoreScheduler,
            LogPartition::getJournal <NodeObject> (), 4, // four read threads for now
                getConfig ().nodeDatabase, getConfig ().ephemeralNodeDatabase))

        , m_sntpClient (SNTPClient::New (*this))

        , m_txQueue (TxQueue::New ())

        , m_validators (add (Validators::Manager::New (
            *this,
            getConfig ().getModuleDatabasePath (),
            LogPartition::getJournal <ValidatorsLog> ())))

        , m_amendmentTable (make_AmendmentTable (weeks(2), MAJORITY_FRACTION, // 204/256 about 80%
            LogPartition::getJournal <AmendmentTableLog> ()))

        , mFeeTrack (LoadFeeTrack::New (LogPartition::getJournal <LoadManagerLog> ()))

        , mHashRouter (IHashRouter::New (IHashRouter::getDefaultHoldTime ()))

        , mValidations (Validations::New ())

        , mProofOfWorkFactory (ProofOfWorkFactory::New ())

        , m_loadManager (LoadManager::New (*this, LogPartition::getJournal <LoadManagerLog> ()))

        , m_sweepTimer (this)

        , mShutdown (false)

        , m_resolver (ResolverAsio::New (m_mainIoPool.getService (), beast::Journal ()))

        , m_io_latency_sampler (m_collectorManager->collector()->make_event ("ios_latency"),
            LogPartition::getJournal <ApplicationLog> (),
                std::chrono::milliseconds (100), m_mainIoPool.getService())
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
        //  The reason is that the unit tests require the Application object to
        //  be created (since so much code calls getApp()). But we don't actually
        //  start all the threads, sockets, and services when running the unit
        //  tests. Therefore anything which needs to be stopped will not get
        //  stopped correctly if it is started in this constructor.
        //

        // VFALCO HACK
        m_nodeStoreScheduler.setJobQueue (*m_jobQueue);

        add (m_ledgerMaster->getPropertySource ());

        // VFALCO TODO remove these once the call is thread safe.
        HashMaps::getInstance ().initializeNonce <size_t> ();
    }

    //--------------------------------------------------------------------------

    CollectorManager& getCollectorManager ()
    {
        return *m_collectorManager;
    }

    FullBelowCache& getFullBelowCache ()
    {
        return *m_fullBelowCache;
    }

    JobQueue& getJobQueue ()
    {
        return *m_jobQueue;
    }

    RPC::Manager& getRPCManager ()
    {
        return *m_rpcManager;
    }

    SiteFiles::Manager& getSiteFiles()
    {
        return *m_siteFiles;
    }

    LocalCredentials& getLocalCredentials ()
    {
        return m_localCredentials ;
    }

    NetworkOPs& getOPs ()
    {
        return *m_networkOPs;
    }

    boost::asio::io_service& getIOService ()
    {
        return m_mainIoPool;
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

    Application::LockType& getMasterLock ()
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

    TxQueue& getTxQueue ()
    {
        return *m_txQueue;
    }

    OrderBookDB& getOrderBookDB ()
    {
        return m_orderBookDB;
    }

    PathRequests& getPathRequests ()
    {
        return *m_pathRequests;
    }

    SLECache& getSLECache ()
    {
        return m_sleCache;
    }

    Validators::Manager& getValidators ()
    {
        return *m_validators;
    }

    AmendmentTable& getAmendmentTable()
    {
        return *m_amendmentTable;
    }

    LoadFeeTrack& getFeeTrack ()
    {
        return *mFeeTrack;
    }

    IHashRouter& getHashRouter ()
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

    ProofOfWorkFactory& getProofOfWorkFactory ()
    {
        return *mProofOfWorkFactory;
    }

    Overlay& overlay ()
    {
        return *m_peers;
    }

    // VFALCO TODO Move these to the .cpp
    bool running ()
    {
        return mTxnDB != nullptr;
    }
    bool getSystemTimeOffset (int& offset)
    {
        return m_sntpClient->getOffset (offset);
    }

    DatabaseCon* getRpcDB ()
    {
        return mRpcDB.get();
    }
    DatabaseCon* getTxnDB ()
    {
        return mTxnDB.get();
    }
    DatabaseCon* getLedgerDB ()
    {
        return mLedgerDB.get();
    }
    DatabaseCon* getWalletDB ()
    {
        return mWalletDB.get();
    }

    bool isShutdown ()
    {
        return mShutdown;
    }

    //--------------------------------------------------------------------------

    static DatabaseCon* openDatabaseCon (const char* fileName,
                                         const char* dbInit[],
                                         int dbCount)
    {
        return new DatabaseCon (fileName, dbInit, dbCount);
    }

    void initSqliteDb (int index)
    {
        switch (index)
        {
        case 0: mRpcDB.reset (openDatabaseCon ("rpc.db", RpcDBInit, RpcDBCount)); break;
        case 1: mTxnDB.reset (openDatabaseCon ("transaction.db", TxnDBInit, TxnDBCount)); break;
        case 2: mLedgerDB.reset (openDatabaseCon ("ledger.db", LedgerDBInit, LedgerDBCount)); break;
        case 3: mWalletDB.reset (openDatabaseCon ("wallet.db", WalletDBInit, WalletDBCount)); break;
        };
    }

    void initSqliteDbs ()
    {
        // VFALCO NOTE DBs are no longer initialized in parallel, since we
        //             dont want unowned threads and because ParallelFor
        //             is broken.
        //
        for (int i = 0; i < 4; ++i)
            initSqliteDb (i);
    }

#ifdef SIGINT
    static void sigIntHandler (int)
    {
        doShutdown = true;
    }
#endif

    // VFALCO TODO Break this function up into many small initialization segments.
    //             Or better yet refactor these initializations into RAII classes
    //             which are members of the Application object.
    //
    void setup ()
    {
        // VFALCO NOTE: 0 means use heuristics to determine the thread count.
        m_jobQueue->setThreadCount (0, getConfig ().RUN_STANDALONE);

    #if ! BEAST_WIN32
    #ifdef SIGINT

        if (!getConfig ().RUN_STANDALONE)
        {
            struct sigaction sa;
            memset (&sa, 0, sizeof (sa));
            sa.sa_handler = &ApplicationImp::sigIntHandler;
            sigaction (SIGINT, &sa, nullptr);
        }

    #endif
    #endif

        assert (mTxnDB == nullptr);

        if (!getConfig ().DEBUG_LOGFILE.empty ())
        {
            // Let debug messages go to the file but only WARNING or higher to regular output (unless verbose)
            LogSink::get()->setLogFile (getConfig ().DEBUG_LOGFILE);

            if (LogSink::get()->getMinSeverity () > lsDEBUG)
                LogPartition::setSeverity (lsDEBUG);
        }

        if (!getConfig().CONSOLE_LOG_OUTPUT.empty())
        {
            LogPartition::setConsoleOutput (getConfig().CONSOLE_LOG_OUTPUT);
        }

        if (!getConfig ().RUN_STANDALONE)
            m_sntpClient->init (getConfig ().SNTP_SERVERS);

        initSqliteDbs ();

        getApp().getLedgerDB ()->getDB ()->executeSQL (boost::str (boost::format ("PRAGMA cache_size=-%d;") %
                (getConfig ().getSize (siLgrDBCache) * 1024)));
        getApp().getTxnDB ()->getDB ()->executeSQL (boost::str (boost::format ("PRAGMA cache_size=-%d;") %
                (getConfig ().getSize (siTxnDBCache) * 1024)));

        mTxnDB->getDB ()->setupCheckpointing (m_jobQueue.get());
        mLedgerDB->getDB ()->setupCheckpointing (m_jobQueue.get());

        if (!getConfig ().RUN_STANDALONE)
            updateTables ();

        m_amendmentTable->addInitial();
        Pathfinder::initPathTable ();

        m_ledgerMaster->setMinValidations (getConfig ().VALIDATION_QUORUM);

        auto const startUp = getConfig ().START_UP;
        if (startUp == Config::FRESH)
        {
            m_journal.info << "Starting new Ledger";

            startNewLedger ();
        }
        else if (startUp == Config::LOAD ||
                 startUp == Config::LOAD_FILE ||
                 startUp == Config::REPLAY)
        {
            m_journal.info << "Loading specified Ledger";

            if (!loadOldLedger (getConfig ().START_LEDGER,
                                startUp == Config::REPLAY,
                                startUp == Config::LOAD_FILE))
            {
                // wtf?
                getApp().signalStop ();
                exit (-1);
            }
        }
        else if (startUp == Config::NETWORK)
        {
            // This should probably become the default once we have a stable network.
            if (!getConfig ().RUN_STANDALONE)
                m_networkOPs->needNetworkLedger ();

            startNewLedger ();
        }
        else
            startNewLedger ();

        m_orderBookDB.setup (getApp().getLedgerMaster ().getCurrentLedger ());

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
        if (!getConfig ().RUN_STANDALONE)
            getUNL ().nodeBootstrap ();

        mValidations->tune (getConfig ().getSize (siValidationsSize), getConfig ().getSize (siValidationsAge));
        m_nodeStore->tune (getConfig ().getSize (siNodeCacheSize), getConfig ().getSize (siNodeCacheAge));
        m_ledgerMaster->tune (getConfig ().getSize (siLedgerSize), getConfig ().getSize (siLedgerAge));
        m_sleCache.setTargetSize (getConfig ().getSize (siSLECacheSize));
        m_sleCache.setTargetAge (getConfig ().getSize (siSLECacheAge));
        SHAMap::setTreeCache (getConfig ().getSize (siTreeCacheSize), getConfig ().getSize (siTreeCacheAge));


        //----------------------------------------------------------------------
        //
        //

        // SSL context used for Peer connections.
        {
            m_peerSSLContext.reset (RippleSSLContext::createAnonymous (
                getConfig ().PEER_SSL_CIPHER_LIST));

            // VFALCO NOTE, It seems the WebSocket context never has
            // set_verify_mode called, for either setting of WEBSOCKET_SECURE
            m_peerSSLContext->get().set_verify_mode (boost::asio::ssl::verify_none);
        }

        // VFALCO NOTE Unfortunately, in stand-alone mode some code still
        //             foolishly calls overlay(). When this is fixed we can
        //             move the instantiation inside a conditional:
        //
        //             if (!getConfig ().RUN_STANDALONE)
        m_peers = make_Overlay (m_mainIoPool, *m_resourceManager,
            *m_siteFiles, getConfig ().getModuleDatabasePath (),
            *m_resolver, m_mainIoPool, m_peerSSLContext->get ());
        // add to Stoppable
        add (*m_peers);

        // SSL context used for WebSocket connections.
        if (getConfig ().WEBSOCKET_SECURE)
        {
            m_wsSSLContext.reset (RippleSSLContext::createAuthenticated (
                getConfig ().WEBSOCKET_SSL_KEY,
                getConfig ().WEBSOCKET_SSL_CERT,
                getConfig ().WEBSOCKET_SSL_CHAIN));
        }
        else
        {
            m_wsSSLContext.reset (RippleSSLContext::createWebSocket ());
        }

        // Create private listening WebSocket socket
        //
        if (!getConfig ().WEBSOCKET_IP.empty () && getConfig ().WEBSOCKET_PORT)
        {
            m_wsPrivateDoor.reset (WSDoor::New (*m_resourceManager,
                getOPs(), getConfig ().WEBSOCKET_IP,
                    getConfig ().WEBSOCKET_PORT, false, false,
                        m_wsSSLContext->get ()));

            if (m_wsPrivateDoor == nullptr)
            {
                beast::FatalError ("Could not open the WebSocket private interface.",
                    __FILE__, __LINE__);
            }
        }
        else
        {
            m_journal.info << "WebSocket private interface: disabled";
        }

        // Create public listening WebSocket socket
        //
        if (!getConfig ().WEBSOCKET_PUBLIC_IP.empty () && getConfig ().WEBSOCKET_PUBLIC_PORT)
        {
            m_wsPublicDoor.reset (WSDoor::New (*m_resourceManager,
                getOPs(), getConfig ().WEBSOCKET_PUBLIC_IP,
                    getConfig ().WEBSOCKET_PUBLIC_PORT, true, false,
                        m_wsSSLContext->get ()));

            if (m_wsPublicDoor == nullptr)
            {
                beast::FatalError ("Could not open the WebSocket public interface.",
                    __FILE__, __LINE__);
            }
        }
        else
        {
            m_journal.info << "WebSocket public interface: disabled";
        }
        if (!getConfig ().WEBSOCKET_PROXY_IP.empty () && getConfig ().WEBSOCKET_PROXY_PORT)
        {
            m_wsProxyDoor.reset (WSDoor::New (*m_resourceManager,
                getOPs(), getConfig ().WEBSOCKET_PROXY_IP,
                    getConfig ().WEBSOCKET_PROXY_PORT, true, true,
                        m_wsSSLContext->get ()));

            if (m_wsProxyDoor == nullptr)
            {
                beast::FatalError ("Could not open the WebSocket public interface.",
                    __FILE__, __LINE__);
            }
        }

        //
        //
        //----------------------------------------------------------------------

        //
        // Allow RPC connections.
        //
        if (! getConfig ().getRpcIP().empty () && getConfig ().getRpcPort() != 0)
        {
            try
            {
                m_rpcDoor.reset (RPCDoor::New (m_mainIoPool, m_rpcServerHandler));
            }
            catch (const std::exception& e)
            {
                // Must run as directed or exit.
                m_journal.fatal <<
                    "Can not open RPC service: " << e.what ();

                exit (3);
            }
        }
        else
        {
            m_journal.info << "RPC interface: disabled";
        }

        //
        // Begin connecting to network.
        //
        if (!getConfig ().RUN_STANDALONE)
        {
            // Should this message be here, conceptually? In theory this sort
            // of message, if displayed, should be displayed from PeerFinder.
            if (getConfig ().PEER_PRIVATE && getConfig ().IPS.empty ())
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

    // Initialize the Validators object with Config information.
    void prepareValidators ()
    {
#if 1
        {
            std::vector <std::string> const& strings (getConfig().validators);
            m_validators->addStrings ("rippled.cfg", strings);
        }
#endif

#if 1
        if (! getConfig().getValidatorsURL().empty())
        {
            m_validators->addURL (getConfig().getValidatorsURL());
        }
#endif

#if 1
        if (getConfig().getValidatorsFile() != beast::File::nonexistent ())
        {
            m_validators->addFile (getConfig().getValidatorsFile());
        }
#endif
    }

    //--------------------------------------------------------------------------
    //
    // Stoppable
    //

    void onPrepare ()
    {
        prepareValidators ();
    }

    void onStart ()
    {
        m_journal.debug << "Application starting";

        m_sweepTimer.setExpiration (10);

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

        // VFALCO TODO get rid of this flag
        mShutdown = true;

        mValidations->flush ();
        mShutdown = false;

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
            if (!getConfig ().RUN_STANDALONE)
            {
                // VFALCO NOTE This seems unnecessary. If we properly refactor the load
                //             manager then the deadlock detector can just always be "armed"
                //
                getApp().getLoadManager ().activateDeadlockDetector ();
            }
        }

        // Wait for the stop signal
#ifdef SIGINT
        for(;;)
        {
            bool const signaled (m_stop.wait (100));
            if (signaled)
                break;
            // VFALCO NOTE It is unfortunate that we have to resort to
            //             polling but thats what the signal() interface
            //             forces us to do.
            //
            if (doShutdown)
                break;
        }
#else
        m_stop.wait ();
#endif

        // Stop the server. When this returns, all
        // Stoppable objects should be stopped.

        doStop ();

        {
            // These two asssignment should no longer be necessary
            // once the WSDoor cancels its pending I/O correctly
            //m_wsPublicDoor = nullptr;
            //m_wsPrivateDoor = nullptr;
            //m_wsProxyDoor = nullptr;

            // VFALCO TODO Try to not have to do this early, by using observers to
            //             eliminate LoadManager's dependency inversions.
            //
            // This deletes the object and therefore, stops the thread.
            //m_loadManager = nullptr;

            m_journal.info << "Done.";

            // VFALCO NOTE This is a sign that something is wrong somewhere, it
            //             shouldn't be necessary to sleep until some flag is set.
            while (mShutdown)
                std::this_thread::sleep_for (std::chrono::milliseconds (100));
        }
    }

    void doStop ()
    {
        m_journal.info << "Received shutdown request";
        StopSustain ();

        stop (m_journal);
    }

    void signalStop ()
    {
        // Unblock the main thread (which is sitting in run()).
        //
        m_stop.signal();
    }

    void onDeadlineTimer (beast::DeadlineTimer& timer)
    {
        if (timer == m_sweepTimer)
        {
            // VFALCO TODO Move all this into doSweep

            boost::filesystem::space_info space = boost::filesystem::space (getConfig ().DATA_DIR);

            // VFALCO TODO Give this magic constant a name and move it into a well documented header
            //
            if (space.available < (512 * 1024 * 1024))
            {
                m_journal.fatal << "Remaining free disk space is less than 512MB";
                getApp().signalStop ();
            }

            m_jobQueue->addJob(jtSWEEP, "sweep",
                std::bind(&ApplicationImp::doSweep, this,
                          std::placeholders::_1));
        }
    }

    void doSweep (Job& j)
    {
        // VFALCO NOTE Does the order of calls matter?
        // VFALCO TODO fix the dependency inversion using an observer,
        //         have listeners register for "onSweep ()" notification.
        //

        m_fullBelowCache->sweep ();

        logTimedCall (m_journal.warning, "TransactionMaster::sweep", __FILE__, __LINE__, std::bind (
            &TransactionMaster::sweep, &m_txMaster));

        logTimedCall (m_journal.warning, "NodeStore::sweep", __FILE__, __LINE__, std::bind (
            &NodeStore::Database::sweep, m_nodeStore.get ()));

        logTimedCall (m_journal.warning, "LedgerMaster::sweep", __FILE__, __LINE__, std::bind (
            &LedgerMaster::sweep, m_ledgerMaster.get()));

        logTimedCall (m_journal.warning, "TempNodeCache::sweep", __FILE__, __LINE__, std::bind (
            &NodeCache::sweep, &m_tempNodeCache));

        logTimedCall (m_journal.warning, "Validations::sweep", __FILE__, __LINE__, std::bind (
            &Validations::sweep, mValidations.get ()));

        logTimedCall (m_journal.warning, "InboundLedgers::sweep", __FILE__, __LINE__, std::bind (
            &InboundLedgers::sweep, &getInboundLedgers ()));

        logTimedCall (m_journal.warning, "SLECache::sweep", __FILE__, __LINE__, std::bind (
            &SLECache::sweep, &m_sleCache));

        logTimedCall (m_journal.warning, "AcceptedLedger::sweep", __FILE__, __LINE__,
            &AcceptedLedger::sweep);

        logTimedCall (m_journal.warning, "SHAMap::sweep", __FILE__, __LINE__,
            &SHAMap::sweep);

        logTimedCall (m_journal.warning, "NetworkOPs::sweepFetchPack", __FILE__, __LINE__, std::bind (
            &NetworkOPs::sweepFetchPack, m_networkOPs.get ()));

        // VFALCO NOTE does the call to sweep() happen on another thread?
        m_sweepTimer.setExpiration (getConfig ().getSize (siSweepInterval));
    }


private:
    void updateTables ();
    void startNewLedger ();
    bool loadOldLedger (
        const std::string& ledgerID, bool replay, bool isFilename);

    void onAnnounceAddress ();
};

//------------------------------------------------------------------------------

void ApplicationImp::startNewLedger ()
{
    // New stuff.
    RippleAddress   rootSeedMaster      = RippleAddress::createSeedGeneric ("masterpassphrase");
    RippleAddress   rootGeneratorMaster = RippleAddress::createGeneratorPublic (rootSeedMaster);
    RippleAddress   rootAddress         = RippleAddress::createAccountPublic (rootGeneratorMaster, 0);

    // Print enough information to be able to claim root account.
    m_journal.info << "Root master seed: " << rootSeedMaster.humanSeed ();
    m_journal.info << "Root account: " << rootAddress.humanAccountID ();

    {
        Ledger::pointer firstLedger = std::make_shared<Ledger> (rootAddress, SYSTEM_CURRENCY_START);
        assert (!!firstLedger->getAccountState (rootAddress));
        // TODO(david): Add any default amendments
        // TODO(david): Set default fee/reserve
        firstLedger->updateHash ();
        firstLedger->setClosed ();
        firstLedger->setAccepted ();
        m_ledgerMaster->pushLedger (firstLedger);

        Ledger::pointer secondLedger = std::make_shared<Ledger> (true, std::ref (*firstLedger));
        secondLedger->setClosed ();
        secondLedger->setAccepted ();
        m_ledgerMaster->pushLedger (secondLedger, std::make_shared<Ledger> (true, std::ref (*secondLedger)));
        assert (!!secondLedger->getAccountState (rootAddress));
        m_networkOPs->setLastCloseTime (secondLedger->getCloseTimeNC ());
    }
}

bool ApplicationImp::loadOldLedger (
    const std::string& ledgerID, bool replay, bool isFileName)
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
                 if (!reader.parse (ledgerFile, jLedger, false))
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
                     std::uint32_t closeTime = getApp().getOPs().getCloseTimeNC ();
                     std::uint64_t totalCoins = 0;

                     if (ledger.get().isMember ("accountState"))
                     {
                          if (ledger.get().isMember ("ledger_index"))
                          {
                              seq = ledger.get()["ledger_index"].asUInt();
                          }
                          if (ledger.get().isMember ("close_time"))
                          {
                              closeTime = ledger.get()["close_time"].asUInt();
                          }
                          if (ledger.get().isMember ("total_coins"))
                          {
                              totalCoins =
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
                         loadLedger = std::make_shared<Ledger> (seq, closeTime);
                         loadLedger->setTotalCoins(totalCoins);

                         for (Json::UInt index = 0; index < ledger.get().size(); ++index)
                         {
                             Json::Value& entry = ledger.get()[index];

                             uint256 uIndex;
                             uIndex.SetHex (entry["index"].asString());
                             entry.removeMember ("index");

                             STParsedJSON stp ("sle", ledger.get()[index]);
                             // m_journal.info << "json: " << stp.object->getJson(0);

                             if (stp.object && (uIndex.isNonZero()))
                             {
                                 SerializedLedgerEntry sle (*stp.object, uIndex);
                                 bool ok = loadLedger->addSLE (sle);
                                 if (!ok)
                                     m_journal.warning << "Couldn't add serialized ledger: " << uIndex;
                             }
                             else
                             {
                                 m_journal.warning << "Invalid entry in ledger";
                             }
                         }

                         loadLedger->setAccepted();
                     }
                 }
            }
        }
        else if (ledgerID.empty () || (ledgerID == "latest"))
            loadLedger = Ledger::getLastFullLedger ();
        else if (ledgerID.length () == 64)
        {
            // by hash
            uint256 hash;
            hash.SetHex (ledgerID);
            loadLedger = Ledger::loadByHash (hash);

            if (!loadLedger)
            {
                // Try to build the ledger from the back end
                auto il = std::make_shared <InboundLedger> (hash, 0, InboundLedger::fcGENERIC,
                    get_seconds_clock ());
                if (il->checkLocal ())
                    loadLedger = il->getLedger ();
            }

        }
        else // assume by sequence
            loadLedger = Ledger::loadByIndex (
                beast::lexicalCastThrow <std::uint32_t> (ledgerID));

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

            // this is the prior ledger
            loadLedger = Ledger::loadByHash (replayLedger->getParentHash ());
            if (!loadLedger)
            {

                // Try to build the ledger from the back end
                auto il = std::make_shared <InboundLedger> (
                    replayLedger->getParentHash(), 0, InboundLedger::fcGENERIC,
                    get_seconds_clock ());
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

        m_journal.info << "Loading ledger " << loadLedger->getHash () << " seq:" << loadLedger->getLedgerSeq ();

        if (loadLedger->getAccountHash ().isZero ())
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

        m_ledgerMaster->setLedgerRangePresent (loadLedger->getLedgerSeq (), loadLedger->getLedgerSeq ());

        Ledger::pointer openLedger = std::make_shared<Ledger> (false, std::ref (*loadLedger));
        m_ledgerMaster->switchLedgers (loadLedger, openLedger);
        m_ledgerMaster->forceValid(loadLedger);
        m_networkOPs->setLastCloseTime (loadLedger->getCloseTimeNC ());

        if (replay)
        {
            // inject transaction(s) from the replayLedger into our open ledger
            SHAMap::ref txns = replayLedger->peekTransactionMap();

            // Get a mutable snapshot of the open ledger
            Ledger::pointer cur = getLedgerMaster().getCurrentLedger();
            cur = std::make_shared <Ledger> (*cur, true);
            assert (!cur->isImmutable());

            for (auto it = txns->peekFirstItem(); it != nullptr;
                 it = txns->peekNextItem(it->getTag()))
            {
                Transaction::pointer txn = replayLedger->getTransaction(it->getTag());
                m_journal.info << txn->getJson(0);
                Serializer s;
                txn->getSTransaction()->add(s);
                if (!cur->addTransaction(it->getTag(), s))
                    m_journal.warning << "Unable to add transaction " << it->getTag();
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

bool serverOkay (std::string& reason)
{
    if (!getConfig ().ELB_SUPPORT)
        return true;

    if (getApp().isShutdown ())
    {
        reason = "Server is shutting down";
        return false;
    }

    if (getApp().getOPs ().isNeedNetworkLedger ())
    {
        reason = "Not synchronized with network yet";
        return false;
    }

    if (getApp().getOPs ().getOperatingMode () < NetworkOPs::omSYNCING)
    {
        reason = "Not synchronized with network";
        return false;
    }

    if (!getApp().getLedgerMaster().isCaughtUp(reason))
        return false;

    if (getApp().getFeeTrack ().isLoadedLocal ())
    {
        reason = "Too much load";
        return false;
    }

    if (getApp().getOPs ().isAmendmentBlocked ())
    {
        reason = "Server version too old";
        return false;
    }

    return true;
}

//VFALCO TODO clean this up since it is just a file holding a single member function definition

static std::vector<std::string> getSchema (DatabaseCon* dbc, const std::string& dbName)
{
    std::vector<std::string> schema;

    std::string sql = "SELECT sql FROM sqlite_master WHERE tbl_name='";
    sql += dbName;
    sql += "';";

    SQL_FOREACH (dbc->getDB (), sql)
    {
        dbc->getDB ()->getStr ("sql", sql);
        schema.push_back (sql);
    }

    return schema;
}

static bool schemaHas (DatabaseCon* dbc, const std::string& dbName, int line, const std::string& content)
{
    std::vector<std::string> schema = getSchema (dbc, dbName);

    if (static_cast<int> (schema.size ()) <= line)
    {
        Log (lsFATAL) << "Schema for " << dbName << " has too few lines";
        throw std::runtime_error ("bad schema");
    }

    return schema[line].find (content) != std::string::npos;
}

static void addTxnSeqField ()
{
    if (schemaHas (getApp().getTxnDB (), "AccountTransactions", 0, "TxnSeq"))
        return;

    Log (lsWARNING) << "Transaction sequence field is missing";

    Database* db = getApp().getTxnDB ()->getDB ();

    std::vector< std::pair<uint256, int> > txIDs;
    txIDs.reserve (300000);

    Log (lsINFO) << "Parsing transactions";
    int i = 0;
    uint256 transID;
    SQL_FOREACH (db, "SELECT TransID,TxnMeta FROM Transactions;")
    {
        Blob rawMeta;
        int metaSize = 2048;
        rawMeta.resize (metaSize);
        metaSize = db->getBinary ("TxnMeta", &*rawMeta.begin (), rawMeta.size ());

        if (metaSize > static_cast<int> (rawMeta.size ()))
        {
            rawMeta.resize (metaSize);
            db->getBinary ("TxnMeta", &*rawMeta.begin (), rawMeta.size ());
        }
        else rawMeta.resize (metaSize);

        std::string tid;
        db->getStr ("TransID", tid);
        transID.SetHex (tid, true);

        if (rawMeta.size () == 0)
        {
            txIDs.push_back (std::make_pair (transID, -1));
            Log (lsINFO) << "No metadata for " << transID;
        }
        else
        {
            TransactionMetaSet m (transID, 0, rawMeta);
            txIDs.push_back (std::make_pair (transID, m.getIndex ()));
        }

        if ((++i % 1000) == 0)
            Log (lsINFO) << i << " transactions read";
    }

    Log (lsINFO) << "All " << i << " transactions read";

    db->executeSQL ("BEGIN TRANSACTION;");

    Log (lsINFO) << "Dropping old index";
    db->executeSQL ("DROP INDEX AcctTxIndex;");

    Log (lsINFO) << "Altering table";
    db->executeSQL ("ALTER TABLE AccountTransactions ADD COLUMN TxnSeq INTEGER;");

    boost::format fmt ("UPDATE AccountTransactions SET TxnSeq = %d WHERE TransID = '%s';");
    i = 0;
    for (auto& t : txIDs)
    {
        db->executeSQL (boost::str (fmt % t.second % to_string (t.first)));

        if ((++i % 1000) == 0)
            Log (lsINFO) << i << " transactions updated";
    }

    Log (lsINFO) << "Building new index";
    db->executeSQL ("CREATE INDEX AcctTxIndex ON AccountTransactions(Account, LedgerSeq, TxnSeq, TransID);");
    db->executeSQL ("END TRANSACTION;");
}

void ApplicationImp::updateTables ()
{
    if (getConfig ().nodeDatabase.size () <= 0)
    {
        Log (lsFATAL) << "The [node_db] configuration setting has been updated and must be set";
        StopSustain ();
        exit (1);
    }

    // perform any needed table updates
    assert (schemaHas (getApp().getTxnDB (), "AccountTransactions", 0, "TransID"));
    assert (!schemaHas (getApp().getTxnDB (), "AccountTransactions", 0, "foobar"));
    addTxnSeqField ();

    if (schemaHas (getApp().getTxnDB (), "AccountTransactions", 0, "PRIMARY"))
    {
        Log (lsFATAL) << "AccountTransactions database should not have a primary key";
        StopSustain ();
        exit (1);
    }

    if (getConfig ().doImport)
    {
        NodeStore::DummyScheduler scheduler;
        std::unique_ptr <NodeStore::Database> source (
            m_nodeStoreManager->make_Database ("NodeStore.import", scheduler,
                LogPartition::getJournal <NodeObject> (), 0,
                    getConfig ().importNodeDatabase));

        WriteLog (lsWARNING, NodeObject) <<
            "Node import from '" << source->getName () << "' to '"
                                 << getApp().getNodeStore().getName () << "'.";

        getApp().getNodeStore().import (*source);
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

std::unique_ptr <Application> make_Application ()
{
    return std::make_unique <ApplicationImp> ();
}

Application& getApp ()
{
    return ApplicationImpBase::getInstance ();
}

}
