//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

// VFALCO TODO Clean this global up
volatile bool doShutdown = false;

class Application;

SETUP_LOG (Application)

//------------------------------------------------------------------------------
//
// Specializations for LogPartition names

template <> char const* LogPartition::getPartitionName <Validators> () { return "Validators"; }

//
//------------------------------------------------------------------------------

// VFALCO TODO Move the function definitions into the class declaration
class ApplicationImp
    : public Application
    , public NodeStore::Scheduler
    , LeakChecked <ApplicationImp>
    , PeerFinder::Callback
{
private:
    static ApplicationImp* s_instance;

public:
    static Application& getInstance ()
    {
        bassert (s_instance != nullptr);
        return *s_instance;
    }

    // RAII container for a boost::asio::io_service run by beast threads
    class IoServiceThread
    {
    public:
        IoServiceThread (String const& name,
                         int expectedConcurrency,
                         int numberOfExtraThreads = 0)
            : m_name (name)
            , m_service (expectedConcurrency)
            , m_work (m_service)
        {
            m_threads.ensureStorageAllocated (numberOfExtraThreads);

            for (int i = 0; i < numberOfExtraThreads; ++i)
                m_threads.add (new ServiceThread (m_name, m_service));
        }

        ~IoServiceThread ()
        {
            m_service.stop ();

            // the dtor of m_threads will block until each thread exits.
        }

        // TEMPORARY HACK for compatibility with old code
        void runExtraThreads ()
        {
            for (int i = 0; i < m_threads.size (); ++i)
                m_threads [i]->start ();
        }

        // Run on the callers thread.
        // This will block until stop is issued.
        void run ()
        {
            Thread const* const currentThread (Thread::getCurrentThread());

            String previousThreadName;

            if (currentThread != nullptr)
            {
                previousThreadName = currentThread->getThreadName ();
            }
            else
            {
                // we're on the main thread
                previousThreadName = "main"; // for vanity
            }

            Thread::setCurrentThreadName (m_name);

            m_service.run ();

            Thread::setCurrentThreadName (previousThreadName);
        }

        void stop ()
        {
            m_service.stop ();
        }

        boost::asio::io_service& getService ()
        {
            return m_service;
        }

        operator boost::asio::io_service& ()
        {
            return m_service;
        }

    private:
        class ServiceThread : Thread
        {
        public:
            explicit ServiceThread (String const& name, boost::asio::io_service& service)
                : Thread (name)
                , m_service (service)
            {
                //startThread ();
            }

            ~ServiceThread ()
            {
                m_service.stop ();

                stopThread (-1); // wait forever
            }

            void start ()
            {
                startThread ();
            }

            void run ()
            {
                m_service.run ();
            }

        private:
            boost::asio::io_service& m_service;
        };

    private:
        String const m_name;
        boost::asio::io_service m_service;
        boost::asio::io_service::work m_work;
        OwnedArray <ServiceThread> m_threads;
    };

    //--------------------------------------------------------------------------

    ApplicationImp ()
        : m_mainService ("io",
                         (getConfig ().NODE_SIZE >= 2) ? 2 : 1,
                         (getConfig ().NODE_SIZE >= 2) ? 1 : 0)
        , m_auxService ("auxio", 1, 1)
        , m_networkOPs (NetworkOPs::New (m_ledgerMaster))
        , m_rpcServerHandler (*m_networkOPs)
        , mTempNodeCache ("NodeCache", 16384, 90)
        , mSLECache ("LedgerEntryCache", 4096, 120)
        , mSNTPClient (m_auxService)
        // VFALCO New stuff
        , m_txQueue (TxQueue::New ())
        , m_nodeStore (NodeStore::New (
            getConfig ().nodeDatabase,
            getConfig ().ephemeralNodeDatabase,
            *this))
        , m_validators (Validators::New ())
        , mFeatures (IFeatures::New (2 * 7 * 24 * 60 * 60, 200)) // two weeks, 200/256
        , mFeeVote (IFeeVote::New (10, 50 * SYSTEM_CURRENCY_PARTS, 12.5 * SYSTEM_CURRENCY_PARTS))
        , mFeeTrack (ILoadFeeTrack::New ())
        , mHashRouter (IHashRouter::New (IHashRouter::getDefaultHoldTime ()))
        , mValidations (Validations::New ())
        , mUNL (UniqueNodeList::New ())
        , mProofOfWorkFactory (ProofOfWorkFactory::New ())
        , m_loadManager (LoadManager::New ())
        , mPeerFinder (PeerFinder::New (*this))
        // VFALCO End new stuff
        // VFALCO TODO replace all NULL with nullptr
        , mRpcDB (NULL)
        , mTxnDB (NULL)
        , mLedgerDB (NULL)
        , mWalletDB (NULL) // VFALCO NOTE are all these 'NULL' ctor params necessary?
        , mRPCDoor (NULL)
        , mSweepTimer (m_auxService)
        , mShutdown (false)
    {
        bassert (s_instance == nullptr);
        s_instance = this;

        // VFALCO TODO remove these once the call is thread safe.
        HashMaps::getInstance ().initializeNonce <size_t> ();

        initValidatorsConfig ();
    }

    ~ApplicationImp ()
    {
        stop ();
        m_networkOPs = nullptr;

        // VFALCO TODO Wrap these in ScopedPointer
        if (mTxnDB != nullptr)
        {
            delete mTxnDB;
            mTxnDB = nullptr;
        }

        if (mLedgerDB != nullptr)
        {
            delete mLedgerDB;
            mLedgerDB = nullptr;
        }

        if (mWalletDB != nullptr)
        {
            delete mWalletDB;
            mWalletDB = nullptr;
        }

        bassert (s_instance == this);
        s_instance = nullptr;
    }

    //--------------------------------------------------------------------------

    // Initialize the Validators object with Config information.
    void initValidatorsConfig ()
    {
#if RIPPLE_USE_NEW_VALIDATORS
        {
            std::vector <std::string> const& strings (getConfig().validators);
            if (! strings.empty ())
                m_validators->addStrings (strings);
        }

        if (! getConfig().getValidatorsURL().empty())
        {
            m_validators->addURL (getConfig().getValidatorsURL());
        }

        if (getConfig().getValidatorsFile() != File::nonexistent ())
        {
            m_validators->addFile (getConfig().getValidatorsFile());
        }
#endif
    }

    //--------------------------------------------------------------------------

    static void callScheduledTask (NodeStore::Scheduler::Task* task, Job&)
    {
        task->performScheduledTask ();
    }

    void scheduleTask (NodeStore::Scheduler::Task* task)
    {
        getJobQueue ().addJob (
            jtWRITE,
            "NodeObject::store",
            BIND_TYPE (&ApplicationImp::callScheduledTask, task, P_1));
    }

    //--------------------------------------------------------------------------

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
        return m_mainService;
    }

    LedgerMaster& getLedgerMaster ()
    {
        return m_ledgerMaster;
    }

    InboundLedgers& getInboundLedgers ()
    {
        return m_inboundLedgers;
    }

    TransactionMaster& getMasterTransaction ()
    {
        return mMasterTransaction;
    }

    NodeCache& getTempNodeCache ()
    {
        return mTempNodeCache;
    }

    NodeStore& getNodeStore ()
    {
        return *m_nodeStore;
    }

    JobQueue& getJobQueue ()
    {
        return mJobQueue;
    }

    Application::LockType& getMasterLock ()
    {
        return mMasterLock;
    }

    LoadManager& getLoadManager ()
    {
        return *m_loadManager;
    }

    TxQueue& getTxQueue ()
    {
        return *m_txQueue;
    }

    OrderBookDB& getOrderBookDB ()
    {
        return mOrderBookDB;
    }

    SLECache& getSLECache ()
    {
        return mSLECache;
    }

    Validators& getValidators ()
    {
        return *m_validators;
    }

    IFeatures& getFeatureTable ()
    {
        return *mFeatures;
    }

    ILoadFeeTrack& getFeeTrack ()
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
        return *mUNL;
    }

    ProofOfWorkFactory& getProofOfWorkFactory ()
    {
        return *mProofOfWorkFactory;
    }

    Peers& getPeers ()
    {
        return *m_peers;
    }

    PeerFinder& getPeerFinder ()
    {
        return *mPeerFinder;
    }

    // VFALCO TODO Move these to the .cpp
    bool running ()
    {
        return mTxnDB != NULL;    // VFALCO TODO replace with nullptr when beast is available
    }
    bool getSystemTimeOffset (int& offset)
    {
        return mSNTPClient.getOffset (offset);
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

    // VFALCO TODO Is it really necessary to init the dbs in parallel?
    void initSqliteDbs ()
    {
        int const count = 4;

        ThreadGroup threadGroup (count);
        ParallelFor (threadGroup).loop (count, &ApplicationImp::initSqliteDb, this);
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
        mJobQueue.setThreadCount (0, getConfig ().RUN_STANDALONE);

        mSweepTimer.expires_from_now (boost::posix_time::seconds (10));
        mSweepTimer.async_wait (BIND_TYPE (&ApplicationImp::sweep, this));

        m_loadManager->startThread ();

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

        if (!getConfig ().RUN_STANDALONE)
            mSNTPClient.init (getConfig ().SNTP_SERVERS);

        initSqliteDbs ();

        getApp().getLedgerDB ()->getDB ()->executeSQL (boost::str (boost::format ("PRAGMA cache_size=-%d;") %
                (getConfig ().getSize (siLgrDBCache) * 1024)));
        getApp().getTxnDB ()->getDB ()->executeSQL (boost::str (boost::format ("PRAGMA cache_size=-%d;") %
                (getConfig ().getSize (siTxnDBCache) * 1024)));

        mTxnDB->getDB ()->setupCheckpointing (&mJobQueue);
        mLedgerDB->getDB ()->setupCheckpointing (&mJobQueue);

        if (!getConfig ().RUN_STANDALONE)
            updateTables ();

        mFeatures->addInitialFeatures ();
        Pathfinder::initPathTable ();

        if (getConfig ().START_UP == Config::FRESH)
        {
            WriteLog (lsINFO, Application) << "Starting new Ledger";

            startNewLedger ();
        }
        else if ((getConfig ().START_UP == Config::LOAD) || (getConfig ().START_UP == Config::REPLAY))
        {
            WriteLog (lsINFO, Application) << "Loading specified Ledger";

            if (!loadOldLedger (getConfig ().START_LEDGER, getConfig ().START_UP == Config::REPLAY))
            {
                getApp().stop ();
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

        mOrderBookDB.setup (getApp().getLedgerMaster ().getCurrentLedger ());

        //
        // Begin validation and ip maintenance.
        // - LocalCredentials maintains local information: including identity and network connection persistence information.
        //
        m_localCredentials.start ();

        //
        // Set up UNL.
        //
        if (!getConfig ().RUN_STANDALONE)
            getUNL ().nodeBootstrap ();

        mValidations->tune (getConfig ().getSize (siValidationsSize), getConfig ().getSize (siValidationsAge));
        m_nodeStore->tune (getConfig ().getSize (siNodeCacheSize), getConfig ().getSize (siNodeCacheAge));
        m_ledgerMaster.tune (getConfig ().getSize (siLedgerSize), getConfig ().getSize (siLedgerAge));
        mSLECache.setTargetSize (getConfig ().getSize (siSLECacheSize));
        mSLECache.setTargetAge (getConfig ().getSize (siSLECacheAge));

        m_ledgerMaster.setMinValidations (getConfig ().VALIDATION_QUORUM);

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
        m_peers = Peers::New (m_mainService, m_peerSSLContext->get ());

        // If we're not in standalone mode,
        // prepare ourselves for  networking
        //
        if (!getConfig ().RUN_STANDALONE)
        {
            // Create the listening sockets for peers
            //
            m_peerDoor = PeerDoor::New (
                PeerDoor::sslRequired,
                getConfig ().PEER_IP,
                getConfig ().peerListeningPort,
                m_mainService,
                m_peerSSLContext->get ());

            if (getConfig ().peerPROXYListeningPort != 0)
            {
                // Also listen on a PROXY-only port.
                m_peerProxyDoor = PeerDoor::New (
                    PeerDoor::sslAndPROXYRequired,
                    getConfig ().PEER_IP,
                    getConfig ().peerPROXYListeningPort,
                    m_mainService,
                    m_peerSSLContext->get ());
            }
        }
        else
        {
            WriteLog (lsINFO, Application) << "Peer interface: disabled";
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
            m_wsPrivateDoor = WSDoor::New (getOPs(), getConfig ().WEBSOCKET_IP,
                getConfig ().WEBSOCKET_PORT, false, m_wsSSLContext->get ());

            if (m_wsPrivateDoor == nullptr)
            {
                FatalError ("Could not open the WebSocket private interface.",
                    __FILE__, __LINE__);
            }
        }
        else
        {
            WriteLog (lsINFO, Application) << "WebSocket private interface: disabled";
        }

        // Create public listening WebSocket socket
        //
        if (!getConfig ().WEBSOCKET_PUBLIC_IP.empty () && getConfig ().WEBSOCKET_PUBLIC_PORT)
        {
            m_wsPublicDoor = WSDoor::New (getOPs(), getConfig ().WEBSOCKET_PUBLIC_IP,
                getConfig ().WEBSOCKET_PUBLIC_PORT, true, m_wsSSLContext->get ());

            if (m_wsPublicDoor == nullptr)
            {
                FatalError ("Could not open the WebSocket public interface.",
                    __FILE__, __LINE__);
            }
        }
        else
        {
            WriteLog (lsINFO, Application) << "WebSocket public interface: disabled";
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
                mRPCDoor = RPCDoor::New (m_mainService, m_rpcServerHandler);
            }
            catch (const std::exception& e)
            {
                // Must run as directed or exit.
                WriteLog (lsFATAL, Application) <<
                    "Can not open RPC service: " << e.what ();

                exit (3);
            }
        }
        else
        {
            WriteLog (lsINFO, Application) << "RPC interface: disabled";
        }

        //
        // Begin connecting to network.
        //
        if (!getConfig ().RUN_STANDALONE)
            m_peers->start ();

        if (getConfig ().RUN_STANDALONE)
        {
            WriteLog (lsWARNING, Application) << "Running in standalone mode";

            m_networkOPs->setStandAlone ();
        }
        else
        {
            // VFALCO NOTE the state timer resets the deadlock detector.
            //
            m_networkOPs->setStateTimer ();
        }
    }

    void run ();
    void stop ();
    void sweep ();
    void doSweep (Job&);

private:
    void updateTables ();
    void startNewLedger ();
    bool loadOldLedger (const std::string&, bool);

    void onAnnounceAddress ();

private:
    Application::LockType mMasterLock;

    IoServiceThread m_mainService;
    IoServiceThread m_auxService;

    LocalCredentials   m_localCredentials;
    LedgerMaster       m_ledgerMaster;
    InboundLedgers     m_inboundLedgers;
    TransactionMaster  mMasterTransaction;
    ScopedPointer <NetworkOPs> m_networkOPs;
    RPCServerHandler   m_rpcServerHandler;
    NodeCache          mTempNodeCache;
    SLECache           mSLECache;
    SNTPClient         mSNTPClient;
    JobQueue           mJobQueue;
    OrderBookDB        mOrderBookDB;

    // VFALCO Clean stuff
    ScopedPointer <SSLContext> m_peerSSLContext;
    ScopedPointer <SSLContext> m_wsSSLContext;
    ScopedPointer <TxQueue> m_txQueue;
    ScopedPointer <NodeStore> m_nodeStore;
    ScopedPointer <Validators> m_validators;
    ScopedPointer <IFeatures> mFeatures;
    ScopedPointer <IFeeVote> mFeeVote;
    ScopedPointer <ILoadFeeTrack> mFeeTrack;
    ScopedPointer <IHashRouter> mHashRouter;
    ScopedPointer <Validations> mValidations;
    ScopedPointer <UniqueNodeList> mUNL;
    ScopedPointer <ProofOfWorkFactory> mProofOfWorkFactory;
    ScopedPointer <Peers> m_peers;
    ScopedPointer <LoadManager> m_loadManager;
    ScopedPointer <PeerDoor> m_peerDoor;
    ScopedPointer <PeerDoor> m_peerProxyDoor;
    ScopedPointer <WSDoor>   m_wsPublicDoor;
    ScopedPointer <WSDoor>   m_wsPrivateDoor;
    ScopedPointer <PeerFinder> mPeerFinder;
    // VFALCO End Clean stuff

    DatabaseCon* mRpcDB;
    DatabaseCon* mTxnDB;
    DatabaseCon* mLedgerDB;
    DatabaseCon* mWalletDB;

    ScopedPointer <RPCDoor>  mRPCDoor;

    boost::asio::deadline_timer mSweepTimer;

    bool volatile mShutdown;
};

// VFALCO TODO Why do we even have this function?
//             It could just be handled in the destructor.
//
void ApplicationImp::stop ()
{
    WriteLog (lsINFO, Application) << "Received shutdown request";

    StopSustain ();
    mShutdown = true;
    m_mainService.stop ();
    m_nodeStore = nullptr;
    mValidations->flush ();
    m_auxService.stop ();
    mJobQueue.shutdown ();

    //WriteLog (lsINFO, Application) << "Stopped: " << mIOService.stopped 

    mShutdown = false;
}

void ApplicationImp::run ()
{
    {
        // VFALCO TODO The unit tests crash if we try to
        //             run these threads in the IoService constructor
        //             so this hack makes them start later.
        //
        m_mainService.runExtraThreads ();
        m_auxService.runExtraThreads ();

        if (!getConfig ().RUN_STANDALONE)
        {
            // VFALCO NOTE This seems unnecessary. If we properly refactor the load
            //             manager then the deadlock detector can just always be "armed"
            //
            getApp().getLoadManager ().activateDeadlockDetector ();
        }
    }

    //--------------------------------------------------------------------------
    //
    //

    // We use the main thread to call io_service::run.
    // What else would we have it do? It blocks until the server
    // eventually gets a stop command.
    //
    m_mainService.run ();

    //
    //
    //--------------------------------------------------------------------------

    {
        m_wsPublicDoor = nullptr;
        m_wsPrivateDoor = nullptr;

        // VFALCO TODO Try to not have to do this early, by using observers to
        //             eliminate LoadManager's dependency inversions.
        //
        // This deletes the object and therefore, stops the thread.
        m_loadManager = nullptr;

        mSweepTimer.cancel();

        WriteLog (lsINFO, Application) << "Done.";

        // VFALCO NOTE This is a sign that something is wrong somewhere, it
        //             shouldn't be necessary to sleep until some flag is set.
        while (mShutdown)
            boost::this_thread::sleep (boost::posix_time::milliseconds (100));
    }
}

void ApplicationImp::sweep ()
{
    boost::filesystem::space_info space = boost::filesystem::space (getConfig ().DATA_DIR);

    // VFALCO TODO Give this magic constant a name and move it into a well documented header
    //
    if (space.available < (512 * 1024 * 1024))
    {
        WriteLog (lsFATAL, Application) << "Remaining free disk space is less than 512MB";
        getApp().stop ();
    }

    mJobQueue.addJob(jtSWEEP, "sweep",
        BIND_TYPE(&ApplicationImp::doSweep, this, P_1));
}

void ApplicationImp::doSweep(Job& j)
{
    // VFALCO NOTE Does the order of calls matter?
    // VFALCO TODO fix the dependency inversion using an observer,
    //         have listeners register for "onSweep ()" notification.
    //

    logTimedCall <Application> ("TransactionMaster::sweep", __FILE__, __LINE__, boost::bind (
        &TransactionMaster::sweep, &mMasterTransaction));

    logTimedCall <Application> ("NodeStore::sweep", __FILE__, __LINE__, boost::bind (
        &NodeStore::sweep, m_nodeStore.get ()));

    logTimedCall <Application> ("LedgerMaster::sweep", __FILE__, __LINE__, boost::bind (
        &LedgerMaster::sweep, &m_ledgerMaster));

    logTimedCall <Application> ("TempNodeCache::sweep", __FILE__, __LINE__, boost::bind (
        &NodeCache::sweep, &mTempNodeCache));

    logTimedCall <Application> ("Validations::sweep", __FILE__, __LINE__, boost::bind (
        &Validations::sweep, mValidations.get ()));

    logTimedCall <Application> ("InboundLedgers::sweep", __FILE__, __LINE__, boost::bind (
        &InboundLedgers::sweep, &getInboundLedgers ()));

    logTimedCall <Application> ("SLECache::sweep", __FILE__, __LINE__, boost::bind (
        &SLECache::sweep, &mSLECache));

    logTimedCall <Application> ("AcceptedLedger::sweep", __FILE__, __LINE__,
        &AcceptedLedger::sweep);

    logTimedCall <Application> ("SHAMap::sweep", __FILE__, __LINE__,
        &SHAMap::sweep);

    logTimedCall <Application> ("NetworkOPs::sweepFetchPack", __FILE__, __LINE__, boost::bind (
        &NetworkOPs::sweepFetchPack, m_networkOPs.get ()));

    // VFALCO NOTE does the call to sweep() happen on another thread?
    mSweepTimer.expires_from_now (boost::posix_time::seconds (getConfig ().getSize (siSweepInterval)));
    mSweepTimer.async_wait (BIND_TYPE (&ApplicationImp::sweep, this));
}

void ApplicationImp::startNewLedger ()
{
    // New stuff.
    RippleAddress   rootSeedMaster      = RippleAddress::createSeedGeneric ("masterpassphrase");
    RippleAddress   rootGeneratorMaster = RippleAddress::createGeneratorPublic (rootSeedMaster);
    RippleAddress   rootAddress         = RippleAddress::createAccountPublic (rootGeneratorMaster, 0);

    // Print enough information to be able to claim root account.
    WriteLog (lsINFO, Application) << "Root master seed: " << rootSeedMaster.humanSeed ();
    WriteLog (lsINFO, Application) << "Root account: " << rootAddress.humanAccountID ();

    {
        Ledger::pointer firstLedger = boost::make_shared<Ledger> (rootAddress, SYSTEM_CURRENCY_START);
        assert (!!firstLedger->getAccountState (rootAddress));
        // WRITEME: Add any default features
        // WRITEME: Set default fee/reserve
        firstLedger->updateHash ();
        firstLedger->setClosed ();
        firstLedger->setAccepted ();
        m_ledgerMaster.pushLedger (firstLedger);

        Ledger::pointer secondLedger = boost::make_shared<Ledger> (true, boost::ref (*firstLedger));
        secondLedger->setClosed ();
        secondLedger->setAccepted ();
        m_ledgerMaster.pushLedger (secondLedger, boost::make_shared<Ledger> (true, boost::ref (*secondLedger)));
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
            WriteLog (lsFATAL, Application) << "No Ledger found?" << std::endl;
            return false;
        }

        if (bReplay)
        { // Replay a ledger close with same prior ledger and transactions
            replayLedger = loadLedger; // this ledger holds the transactions we want to replay
            loadLedger = Ledger::loadByIndex (replayLedger->getLedgerSeq() - 1); // this is the prior ledger
            if (!loadLedger || (replayLedger->getParentHash() != loadLedger->getHash()))
            {
                WriteLog (lsFATAL, Application) << "Replay ledger missing/damaged";
                assert (false);
                return false;
            }
        }

        loadLedger->setClosed ();

        WriteLog (lsINFO, Application) << "Loading ledger " << loadLedger->getHash () << " seq:" << loadLedger->getLedgerSeq ();

        if (loadLedger->getAccountHash ().isZero ())
        {
            WriteLog (lsFATAL, Application) << "Ledger is empty.";
            assert (false);
            return false;
        }

        if (!loadLedger->walkLedger ())
        {
            WriteLog (lsFATAL, Application) << "Ledger is missing nodes.";
            return false;
        }

        if (!loadLedger->assertSane ())
        {
            WriteLog (lsFATAL, Application) << "Ledger is not sane.";
            return false;
        }

        m_ledgerMaster.setLedgerRangePresent (loadLedger->getLedgerSeq (), loadLedger->getLedgerSeq ());

        Ledger::pointer openLedger = boost::make_shared<Ledger> (false, boost::ref (*loadLedger));
        m_ledgerMaster.switchLedgers (loadLedger, openLedger);
        m_ledgerMaster.forceValid(loadLedger);
        m_networkOPs->setLastCloseTime (loadLedger->getCloseTimeNC ());

        if (bReplay)
        { // inject transaction from replayLedger into consensus set
            SHAMap::ref txns = replayLedger->peekTransactionMap();
            Ledger::ref cur = getLedgerMaster().getCurrentLedger();

            for (SHAMapItem::pointer it = txns->peekFirstItem(); it != nullptr; it = txns->peekNextItem(it->getTag()))
            {
                Transaction::pointer txn = replayLedger->getTransaction(it->getTag());
                WriteLog (lsINFO, Application) << txn->getJson(0);
                Serializer s;
                txn->getSTransaction()->add(s);
                if (!cur->addTransaction(it->getTag(), s))
                {
                    WriteLog (lsWARNING, Application) << "Unable to add transaction " << it->getTag();
                }
            }
        }
    }
    catch (SHAMapMissingNode&)
    {
        WriteLog (lsFATAL, Application) << "Data is missing for selected ledger";
        return false;
    }
    catch (boost::bad_lexical_cast&)
    {
        WriteLog (lsFATAL, Application) << "Ledger specified '" << l << "' is not valid";
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

    if (getConfig ().importNodeDatabase.size () > 0)
    {
        ScopedPointer <NodeStore> source (NodeStore::New (getConfig ().importNodeDatabase));

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

Application* Application::New ()
{
    ScopedPointer <Application> object (new ApplicationImp);
    return object.release();
}

Application& getApp ()
{
    return ApplicationImp::getInstance ();
}

#if 0
#if RIPPLE_APPLICATION_CLEAN_EXIT
    // Application object will be deleted on exit. If the code doesn't exit
    // cleanly this could cause hangs or crashes on exit.
    //
    SingletonLifetime::Lifetime lifetime (SingletonLifetime::persistAfterCreation);

#else
    // This will make it so that the Application object is not deleted on exit.
    //
    SingletonLifetime::Lifetime lifetime (SingletonLifetime::neverDestroyed);

#endif

    return *SharedSingleton <ApplicationImp>::getInstance (lifetime);
#endif

//------------------------------------------------------------------------------

// Holds a loaned object. The destructor returns it to the source.
template <typename Object>
class LoanedObjectHolder : public SharedObject
{
public:
    class Owner
    {
    public:
        virtual void recoverLoanedObject (Object* object) = 0;
    };

    // The class that loans out the object uses this constructor
    LoanedObjectHolder (Owner* owner, Object* object)
        : m_owner (owner)
        , m_object (object)
    {
    }

    ~LoanedObjectHolder ()
    {
        m_owner->recoverLoanedObject (m_object);
    }

    Object& get ()
    {
        return *m_object;
    }

    Object const& get () const
    {
        return *m_object;
    }

private:
    Owner* m_owner;
    Object* m_object;
};

//------------------------------------------------------------------------------

class LoanedObjectTests : public UnitTest
{
public:
    // Meets the LoaningContainer requirements
    //
    class LoanedObject : public List <LoanedObject>::Node
    {
    public:
        void useful ()
        {
        }
    };

    // Requirements:
    //  Object must be derived from List <Object>::Node
    //
    template <class Object>
    class LoaningContainer : protected LoanedObjectHolder <Object>::Owner
    {
    protected:
        void recoverLoanedObject (Object* object)
        {
            m_list.push_front (*object);
        }

    public:
        typedef Object                      ValueType;
        typedef Object&                     Reference;
        typedef Object*                     Pointer;
        typedef LoanedObjectHolder <Object> Holder;
        typedef SharedPtr <Holder >         Ptr;

        LoaningContainer ()
        {
        }

        ~LoaningContainer ()
        {
            while (! m_list.empty ())
                delete (&m_list.pop_front ());
        }

        bool empty () const
        {
            return m_list.empty ();
        }

        std::size_t size () const
        {
            return m_list.size ();
        }

        // Donate an object that can be loaned out later
        // Ownership is transferred, the object must have been
        // allocated via operator new.
        //
        void donate (Object* object)
        {
            m_list.push_front (*object);
        }

        // Check an object out
        Ptr borrow ()
        {
            if (m_list.empty ())
                return Ptr ();
            
            Object& object (m_list.pop_front ());
            return Ptr (new Holder (this, &object));
        }

    private:
        List <Object> m_list;
    };

    enum
    {
        numberAvailable = 5
    };

    void runTest ()
    {
        beginTestCase ("loan objects");

        typedef LoaningContainer <LoanedObject> Container;

        Container items;

        expect (items.size () == 0);

        for (int i = 0; i < numberAvailable; ++i)
            items.donate (new LoanedObject);

        expect (items.size () == numberAvailable);

        {
            Container::Ptr item (items.borrow());

            item->get().useful ();

            expect (items.size () == numberAvailable - 1);
        }

        expect (items.size () == numberAvailable);
    }

    LoanedObjectTests () : UnitTest ("LoanedObject", "ripple", runManual)
    {
    }
};

static LoanedObjectTests loanedObjectTests;
