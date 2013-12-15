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

// VFALCO TODO Clean this global up
static bool volatile doShutdown = false;

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

template <> char const* LogPartition::getPartitionName <CollectorManager> () { return "Collector"; }

//
//------------------------------------------------------------------------------

// VFALCO TODO Move the function definitions into the class declaration
class ApplicationImp
    : public Application
    , public RootStoppable
    , public DeadlineTimer::Listener
    , public LeakChecked <ApplicationImp>
{
private:
    static ApplicationImp* s_instance;

public:
    static Application& getInstance ()
    {
        bassert (s_instance != nullptr);
        return *s_instance;
    }

    //--------------------------------------------------------------------------

    ApplicationImp ()
        : RootStoppable ("Application")
        , m_journal (LogPartition::getJournal <ApplicationLog> ())
        , m_tempNodeCache ("NodeCache", 16384, 90)
        , m_sleCache ("LedgerEntryCache", 4096, 120)

        , m_collectorManager (CollectorManager::New (
            getConfig().insightSettings,
                LogPartition::getJournal <CollectorManager> ()))

        , m_resourceManager (add (Resource::Manager::New (
            LogPartition::getJournal <ResourceManagerLog> ())))

        , m_rpcServiceManager (RPC::Manager::New (
            LogPartition::getJournal <RPCServiceManagerLog> ()))

        // The JobQueue has to come pretty early since
        // almost everything is a Stoppable child of the JobQueue.
        //
        , m_jobQueue (JobQueue::New (
            m_collectorManager->collector (),
                *this, LogPartition::getJournal <JobQueueLog> ()))

        // The io_service must be a child of the JobQueue since we call addJob
        // in response to newtwork data from peers and also client requests.
        //
        , m_mainIoPool (*m_jobQueue, "io", (getConfig ().NODE_SIZE >= 2) ? 2 : 1)

        //
        // Anything which calls addJob must be a descendant of the JobQueue
        //

        , m_siteFiles (SiteFiles::Manager::New (
            *this, LogPartition::getJournal <SiteFilesLog> ()))

        , m_orderBookDB (*m_jobQueue)

        , m_ledgerMaster (LedgerMaster::New (
            *m_jobQueue, LogPartition::getJournal <LedgerMaster> ()))

        // VFALCO NOTE Does NetworkOPs depend on LedgerMaster?
        , m_networkOPs (NetworkOPs::New (
            *m_ledgerMaster, *m_jobQueue, LogPartition::getJournal <NetworkOPsLog> ()))

        // VFALCO NOTE LocalCredentials starts the deprecated UNL service
        , m_deprecatedUNL (UniqueNodeList::New (*m_jobQueue))

        , m_rpcHTTPServer (RPCHTTPServer::New (*m_networkOPs,
            LogPartition::getJournal <HTTPServerLog> (), *m_jobQueue, *m_networkOPs, *m_resourceManager))

#if ! RIPPLE_USE_RPC_SERVICE_MANAGER
        , m_rpcServerHandler (*m_networkOPs, *m_resourceManager) // passive object, not a Service
#endif

        , m_nodeStoreScheduler (*m_jobQueue, *m_jobQueue)

        , m_nodeStore (NodeStore::Database::New ("NodeStore.main", m_nodeStoreScheduler,
            getConfig ().nodeDatabase, getConfig ().ephemeralNodeDatabase))

        , m_sntpClient (SNTPClient::New (*this))

        , m_inboundLedgers (*m_jobQueue)

        , m_txQueue (TxQueue::New ())

        , m_validators (add (Validators::Manager::New (
            *this, LogPartition::getJournal <ValidatorsLog> ())))

        , mFeatures (IFeatures::New (2 * 7 * 24 * 60 * 60, 200)) // two weeks, 200/256

        , mFeeVote (IFeeVote::New (10, 20 * SYSTEM_CURRENCY_PARTS, 5 * SYSTEM_CURRENCY_PARTS))

        , mFeeTrack (LoadFeeTrack::New (LogPartition::getJournal <LoadManagerLog> ()))

        , mHashRouter (IHashRouter::New (IHashRouter::getDefaultHoldTime ()))

        , mValidations (Validations::New ())

        , mProofOfWorkFactory (ProofOfWorkFactory::New ())

        , m_loadManager (LoadManager::New (*this, LogPartition::getJournal <LoadManagerLog> ()))

        , m_sweepTimer (this)

        , mShutdown (false)
    {
        bassert (s_instance == nullptr);
        s_instance = this;

        add (m_ledgerMaster->getPropertySource ());

        // VFALCO TODO remove these once the call is thread safe.
        HashMaps::getInstance ().initializeNonce <size_t> ();
    }

    ~ApplicationImp ()
    {
        bassert (s_instance == this);
        s_instance = nullptr;
    }

    //--------------------------------------------------------------------------
    
    CollectorManager& getCollectorManager ()
    {
        return *m_collectorManager;
    }

    RPC::Manager& getRPCServiceManager()
    {
        return *m_rpcServiceManager;
    }

    JobQueue& getJobQueue ()
    {
        return *m_jobQueue;
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

    LedgerMaster& getLedgerMaster ()
    {
        return *m_ledgerMaster;
    }

    InboundLedgers& getInboundLedgers ()
    {
        return m_inboundLedgers;
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

    SLECache& getSLECache ()
    {
        return m_sleCache;
    }

    Validators::Manager& getValidators ()
    {
        return *m_validators;
    }

    IFeatures& getFeatureTable ()
    {
        return *mFeatures;
    }

    LoadFeeTrack& getFeeTrack ()
    {
        return *mFeeTrack;
    }

    IFeeVote& getFeeVote ()
    {
        return *mFeeVote;
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

    Peers& getPeers ()
    {
        return *m_peers;
    }

    // VFALCO TODO Move these to the .cpp
    bool running ()
    {
        return mTxnDB != NULL;    // VFALCO TODO replace with nullptr when beast is available
    }
    bool getSystemTimeOffset (int& offset)
    {
        return m_sntpClient->getOffset (offset);
    }

    DatabaseCon* getRpcDB ()
    {
        return mRpcDB;
    }
    DatabaseCon* getTxnDB ()
    {
        return mTxnDB;
    }
    DatabaseCon* getLedgerDB ()
    {
        return mLedgerDB;
    }
    DatabaseCon* getWalletDB ()
    {
        return mWalletDB;
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
        case 0: mRpcDB = openDatabaseCon ("rpc.db", RpcDBInit, RpcDBCount); break;
        case 1: mTxnDB = openDatabaseCon ("transaction.db", TxnDBInit, TxnDBCount); break;
        case 2: mLedgerDB = openDatabaseCon ("ledger.db", LedgerDBInit, LedgerDBCount); break;
        case 3: mWalletDB = openDatabaseCon ("wallet.db", WalletDBInit, WalletDBCount); break;
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
            sigaction (SIGINT, &sa, NULL);
        }

    #endif
    #endif

        assert (mTxnDB == NULL);

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

        mTxnDB->getDB ()->setupCheckpointing (m_jobQueue);
        mLedgerDB->getDB ()->setupCheckpointing (m_jobQueue);

        if (!getConfig ().RUN_STANDALONE)
            updateTables ();

        mFeatures->addInitialFeatures ();
        Pathfinder::initPathTable ();

        m_ledgerMaster->setMinValidations (getConfig ().VALIDATION_QUORUM);

        if (getConfig ().START_UP == Config::FRESH)
        {
            m_journal.info << "Starting new Ledger";

            startNewLedger ();
        }
        else if ((getConfig ().START_UP == Config::LOAD) || (getConfig ().START_UP == Config::REPLAY))
        {
            m_journal.info << "Loading specified Ledger";

            if (!loadOldLedger (getConfig ().START_LEDGER, getConfig ().START_UP == Config::REPLAY))
            {
                // wtf?
                getApp().signalStop ();
                exit (-1);
            }
        }
        else if (getConfig ().START_UP == Config::NETWORK)
        {
            // This should probably become the default once we have a stable network
            if (!getConfig ().RUN_STANDALONE)
                m_networkOPs->needNetworkLedger ();

            startNewLedger ();
        }
        else
            startNewLedger ();

        m_orderBookDB.setup (getApp().getLedgerMaster ().getCurrentLedger ());

        //
        // Begin validation and ip maintenance.
        // - LocalCredentials maintains local information: including identity and network connection persistence information.
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
            m_peerSSLContext = RippleSSLContext::createAnonymous (
                getConfig ().PEER_SSL_CIPHER_LIST);

            // VFALCO NOTE, It seems the WebSocket context never has
            // set_verify_mode called, for either setting of WEBSOCKET_SECURE
            m_peerSSLContext->get().set_verify_mode (boost::asio::ssl::verify_none);
        }

        // VFALCO NOTE Unfortunately, in stand-alone mode some code still
        //             foolishly calls getPeers(). When this is fixed we can move
        //             the creation of the peer SSL context and Peers object into
        //             the conditional.
        //
        m_peers = add (Peers::New (m_mainIoPool, *m_resourceManager, *m_siteFiles,
            m_mainIoPool, m_peerSSLContext->get ()));

        // If we're not in standalone mode,
        // prepare ourselves for  networking
        //
        if (!getConfig ().RUN_STANDALONE)
        {
            // Create the listening sockets for peers
            //
            m_peerDoors.add (PeerDoor::New (
                m_mainIoPool,
                *m_resourceManager,
                PeerDoor::sslRequired,
                getConfig ().PEER_IP,
                getConfig ().peerListeningPort,
                m_mainIoPool,
                m_peerSSLContext->get ()));

            if (getConfig ().peerPROXYListeningPort != 0)
            {
                // Also listen on a PROXY-only port.
                m_peerDoors.add (PeerDoor::New (
                    m_mainIoPool,
                    *m_resourceManager,
                    PeerDoor::sslAndPROXYRequired,
                    getConfig ().PEER_IP,
                    getConfig ().peerPROXYListeningPort,
                    m_mainIoPool,
                    m_peerSSLContext->get ()));
            }
        }
        else
        {
            m_journal.info << "Peer interface: disabled";
        }

        // SSL context used for WebSocket connections.
        if (getConfig ().WEBSOCKET_SECURE)
        {
            m_wsSSLContext = RippleSSLContext::createAuthenticated (
                getConfig ().WEBSOCKET_SSL_KEY,
                getConfig ().WEBSOCKET_SSL_CERT,
                getConfig ().WEBSOCKET_SSL_CHAIN);
        }
        else
        {
            m_wsSSLContext = RippleSSLContext::createWebSocket ();
        }

        // Create private listening WebSocket socket
        //
        if (!getConfig ().WEBSOCKET_IP.empty () && getConfig ().WEBSOCKET_PORT)
        {
            m_wsPrivateDoor = WSDoor::New (*m_resourceManager,
                getOPs(), getConfig ().WEBSOCKET_IP,
                    getConfig ().WEBSOCKET_PORT, false, false,
                        m_wsSSLContext->get ());

            if (m_wsPrivateDoor == nullptr)
            {
                FatalError ("Could not open the WebSocket private interface.",
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
            m_wsPublicDoor = WSDoor::New (*m_resourceManager,
                getOPs(), getConfig ().WEBSOCKET_PUBLIC_IP,
                    getConfig ().WEBSOCKET_PUBLIC_PORT, true, false,
                        m_wsSSLContext->get ());

            if (m_wsPublicDoor == nullptr)
            {
                FatalError ("Could not open the WebSocket public interface.",
                    __FILE__, __LINE__);
            }
        }
        else
        {
            m_journal.info << "WebSocket public interface: disabled";
        }
        if (!getConfig ().WEBSOCKET_PROXY_IP.empty () && getConfig ().WEBSOCKET_PROXY_PORT)
        {
            m_wsProxyDoor = WSDoor::New (*m_resourceManager,
                getOPs(), getConfig ().WEBSOCKET_PROXY_IP,
                    getConfig ().WEBSOCKET_PROXY_PORT, true, true,
                        m_wsSSLContext->get ());

            if (m_wsProxyDoor == nullptr)
            {
                FatalError ("Could not open the WebSocket public interface.",
                    __FILE__, __LINE__);
            }
        }

        //
        //
        //----------------------------------------------------------------------

        //
        // Allow RPC connections.
        //
#if RIPPLE_USE_RPC_SERVICE_MANAGER
        m_rpcHTTPServer->setup (m_journal);

#else
        if (! getConfig ().getRpcIP().empty () && getConfig ().getRpcPort() != 0)
        {
            try
            {
                m_rpcDoor = RPCDoor::New (m_mainIoPool, m_rpcServerHandler);
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
#endif

        //
        // Begin connecting to network.
        //
        if (!getConfig ().RUN_STANDALONE)
        {
            m_peers->start ();
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
        if (getConfig().getValidatorsFile() != File::nonexistent ())
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
    }

    // Called to indicate shutdown.
    void onStop ()
    {
        m_journal.debug << "Application stopping";

        m_sweepTimer.cancel();

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

    void onWrite (PropertyStream& stream)
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
                boost::this_thread::sleep (boost::posix_time::milliseconds (100));
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

    void onDeadlineTimer (DeadlineTimer& timer)
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
                BIND_TYPE(&ApplicationImp::doSweep, this, P_1));
        }
    }

    void doSweep (Job& j)
    {
        // VFALCO NOTE Does the order of calls matter?
        // VFALCO TODO fix the dependency inversion using an observer,
        //         have listeners register for "onSweep ()" notification.
        //

        logTimedCall (m_journal.warning, "TransactionMaster::sweep", __FILE__, __LINE__, boost::bind (
            &TransactionMaster::sweep, &m_txMaster));

        logTimedCall (m_journal.warning, "NodeStore::sweep", __FILE__, __LINE__, boost::bind (
            &NodeStore::Database::sweep, m_nodeStore.get ()));

        logTimedCall (m_journal.warning, "LedgerMaster::sweep", __FILE__, __LINE__, boost::bind (
            &LedgerMaster::sweep, m_ledgerMaster.get()));

        logTimedCall (m_journal.warning, "TempNodeCache::sweep", __FILE__, __LINE__, boost::bind (
            &NodeCache::sweep, &m_tempNodeCache));

        logTimedCall (m_journal.warning, "Validations::sweep", __FILE__, __LINE__, boost::bind (
            &Validations::sweep, mValidations.get ()));

        logTimedCall (m_journal.warning, "InboundLedgers::sweep", __FILE__, __LINE__, boost::bind (
            &InboundLedgers::sweep, &getInboundLedgers ()));

        logTimedCall (m_journal.warning, "SLECache::sweep", __FILE__, __LINE__, boost::bind (
            &SLECache::sweep, &m_sleCache));

        logTimedCall (m_journal.warning, "AcceptedLedger::sweep", __FILE__, __LINE__,
            &AcceptedLedger::sweep);

        logTimedCall (m_journal.warning, "SHAMap::sweep", __FILE__, __LINE__,
            &SHAMap::sweep);

        logTimedCall (m_journal.warning, "NetworkOPs::sweepFetchPack", __FILE__, __LINE__, boost::bind (
            &NetworkOPs::sweepFetchPack, m_networkOPs.get ()));

        // VFALCO NOTE does the call to sweep() happen on another thread?
        m_sweepTimer.setExpiration (getConfig ().getSize (siSweepInterval));
    }


private:
    void updateTables ();
    void startNewLedger ();
    bool loadOldLedger (const std::string&, bool);

    void onAnnounceAddress ();

private:
    Journal m_journal;
    Application::LockType m_masterMutex;

    // These are not Stoppable-derived
    NodeCache m_tempNodeCache;
    SLECache m_sleCache;
    LocalCredentials m_localCredentials;
    TransactionMaster m_txMaster;

    beast::unique_ptr <CollectorManager> m_collectorManager;
    ScopedPointer <Resource::Manager> m_resourceManager;
    ScopedPointer <RPC::Manager> m_rpcServiceManager;

    // These are Stoppable-related
    ScopedPointer <JobQueue> m_jobQueue;
    IoServicePool m_mainIoPool;
    ScopedPointer <SiteFiles::Manager> m_siteFiles;
    OrderBookDB m_orderBookDB;
    ScopedPointer <LedgerMaster> m_ledgerMaster;
    ScopedPointer <NetworkOPs> m_networkOPs;
    ScopedPointer <UniqueNodeList> m_deprecatedUNL;
    ScopedPointer <RPCHTTPServer> m_rpcHTTPServer;
#if ! RIPPLE_USE_RPC_SERVICE_MANAGER
    RPCServerHandler m_rpcServerHandler;
#endif
    NodeStoreScheduler m_nodeStoreScheduler;
    ScopedPointer <NodeStore::Database> m_nodeStore;
    ScopedPointer <SNTPClient> m_sntpClient;
    InboundLedgers m_inboundLedgers;
    ScopedPointer <TxQueue> m_txQueue;
    ScopedPointer <Validators::Manager> m_validators;
    ScopedPointer <IFeatures> mFeatures;
    ScopedPointer <IFeeVote> mFeeVote;
    ScopedPointer <LoadFeeTrack> mFeeTrack;
    ScopedPointer <IHashRouter> mHashRouter;
    ScopedPointer <Validations> mValidations;
    ScopedPointer <ProofOfWorkFactory> mProofOfWorkFactory;
    ScopedPointer <LoadManager> m_loadManager;
    DeadlineTimer m_sweepTimer;
    bool volatile mShutdown;

    ScopedPointer <DatabaseCon> mRpcDB;
    ScopedPointer <DatabaseCon> mTxnDB;
    ScopedPointer <DatabaseCon> mLedgerDB;
    ScopedPointer <DatabaseCon> mWalletDB;

    ScopedPointer <SSLContext> m_peerSSLContext;
    ScopedPointer <SSLContext> m_wsSSLContext;
    ScopedPointer <Peers> m_peers;
    OwnedArray <PeerDoor>  m_peerDoors;
    ScopedPointer <RPCDoor>  m_rpcDoor;
    ScopedPointer <WSDoor> m_wsPublicDoor;
    ScopedPointer <WSDoor> m_wsPrivateDoor;
    ScopedPointer <WSDoor> m_wsProxyDoor;

    WaitableEvent m_stop;
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
        Ledger::pointer firstLedger = boost::make_shared<Ledger> (rootAddress, SYSTEM_CURRENCY_START);
        assert (!!firstLedger->getAccountState (rootAddress));
        // WRITEME: Add any default features
        // WRITEME: Set default fee/reserve
        firstLedger->updateHash ();
        firstLedger->setClosed ();
        firstLedger->setAccepted ();
        m_ledgerMaster->pushLedger (firstLedger);

        Ledger::pointer secondLedger = boost::make_shared<Ledger> (true, boost::ref (*firstLedger));
        secondLedger->setClosed ();
        secondLedger->setAccepted ();
        m_ledgerMaster->pushLedger (secondLedger, boost::make_shared<Ledger> (true, boost::ref (*secondLedger)));
        assert (!!secondLedger->getAccountState (rootAddress));
        m_networkOPs->setLastCloseTime (secondLedger->getCloseTimeNC ());
    }
}

bool ApplicationImp::loadOldLedger (const std::string& l, bool bReplay)
{
    try
    {
        Ledger::pointer loadLedger, replayLedger;

        if (l.empty () || (l == "latest"))
            loadLedger = Ledger::getLastFullLedger ();
        else if (l.length () == 64)
        {
            // by hash
            uint256 hash;
            hash.SetHex (l);
            loadLedger = Ledger::loadByHash (hash);
        }
        else // assume by sequence
            loadLedger = Ledger::loadByIndex (lexicalCastThrow <uint32> (l));

        if (!loadLedger)
        {
            m_journal.fatal << "No Ledger found?" << std::endl;
            return false;
        }

        if (bReplay)
        { // Replay a ledger close with same prior ledger and transactions
            replayLedger = loadLedger; // this ledger holds the transactions we want to replay
            loadLedger = Ledger::loadByIndex (replayLedger->getLedgerSeq() - 1); // this is the prior ledger
            if (!loadLedger || (replayLedger->getParentHash() != loadLedger->getHash()))
            {
                m_journal.fatal << "Replay ledger missing/damaged";
                assert (false);
                return false;
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
            return false;
        }

        if (!loadLedger->assertSane ())
        {
            m_journal.fatal << "Ledger is not sane.";
            return false;
        }

        m_ledgerMaster->setLedgerRangePresent (loadLedger->getLedgerSeq (), loadLedger->getLedgerSeq ());

        Ledger::pointer openLedger = boost::make_shared<Ledger> (false, boost::ref (*loadLedger));
        m_ledgerMaster->switchLedgers (loadLedger, openLedger);
        m_ledgerMaster->forceValid(loadLedger);
        m_networkOPs->setLastCloseTime (loadLedger->getCloseTimeNC ());

        if (bReplay)
        { // inject transaction from replayLedger into consensus set
            SHAMap::ref txns = replayLedger->peekTransactionMap();
            Ledger::ref cur = getLedgerMaster().getCurrentLedger();

            for (SHAMapItem::pointer it = txns->peekFirstItem(); it != nullptr; it = txns->peekNextItem(it->getTag()))
            {
                Transaction::pointer txn = replayLedger->getTransaction(it->getTag());
                m_journal.info << txn->getJson(0);
                Serializer s;
                txn->getSTransaction()->add(s);
                if (!cur->addTransaction(it->getTag(), s))
                {
                    m_journal.warning << "Unable to add transaction " << it->getTag();
                }
            }
        }
    }
    catch (SHAMapMissingNode&)
    {
        m_journal.fatal << "Data is missing for selected ledger";
        return false;
    }
    catch (boost::bad_lexical_cast&)
    {
        m_journal.fatal << "Ledger specified '" << l << "' is not valid";
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

    if (getApp().getOPs ().isFeatureBlocked ())
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

    typedef std::pair<uint256, int> u256_int_pair_t;
    boost::format fmt ("UPDATE AccountTransactions SET TxnSeq = %d WHERE TransID = '%s';");
    i = 0;
    BOOST_FOREACH (u256_int_pair_t & t, txIDs)
    {
        db->executeSQL (boost::str (fmt % t.second % t.first.GetHex ()));

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
        ScopedPointer <NodeStore::Database> source (NodeStore::Database::New (
            "NodeStore.import", scheduler, getConfig ().importNodeDatabase));

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

ApplicationImp* ApplicationImp::s_instance;

//------------------------------------------------------------------------------

Application::Application ()
    : PropertyStream::Source ("app")
{
}

Application* Application::New ()
{
    return new ApplicationImp;
}

Application& getApp ()
{
    return ApplicationImp::getInstance ();
}

// class LoandObject (5removed, use git history to recover)
