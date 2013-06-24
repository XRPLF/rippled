//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

// VFALCO TODO Clean this global up
volatile bool doShutdown = false;

// VFALCO TODO Wrap this up in something neater.
IApplication* theApp = nullptr;

class Application;

SETUP_LOG (Application)

// VFALCO TODO Move the function definitions into the class declaration
class Application : public IApplication
{
public:
    Application ();
    ~Application ();

    LocalCredentials& getLocalCredentials ()
    {
        return m_localCredentials ;
    }
 
    NetworkOPs& getOPs ()
    {
        return mNetOps;
    }

    boost::asio::io_service& getIOService ()
    {
        return mIOService;
    }
    
    boost::asio::io_service& getAuxService ()
    {
        return mAuxService;
    }

    LedgerMaster& getLedgerMaster ()
    {
        return mLedgerMaster;
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
    
    HashedObjectStore& getHashedObjectStore ()
    {
        return mHashedObjectStore;
    }
    
    JobQueue& getJobQueue ()
    {
        return mJobQueue;
    }
    
    boost::recursive_mutex& getMasterLock ()
    {
        return mMasterLock;
    }
    
    ILoadManager& getLoadManager ()
    {
        return *m_loadManager;
    }
    
    TXQueue& getTxnQueue ()
    {
        return mTxnQueue;
    }
    
    PeerDoor& getPeerDoor ()
    {
        return *mPeerDoor;
    }
    
    OrderBookDB& getOrderBookDB ()
    {
        return mOrderBookDB;
    }
    
    SLECache& getSLECache ()
    {
        return mSLECache;
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
    
    IValidations& getValidations ()
    {
        return *mValidations;
    }
    
    IUniqueNodeList& getUNL ()
    {
        return *mUNL;
    }
    
    IProofOfWorkFactory& getProofOfWorkFactory ()
    {
        return *mProofOfWorkFactory;
    }
    
    IPeers& getPeers ()
    {
        return *mPeers;
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
    DatabaseCon* getNetNodeDB ()
    {
        return mNetNodeDB;
    }
    DatabaseCon* getPathFindDB ()
    {
        return mPathFindDB;
    }
    DatabaseCon* getHashNodeDB ()
    {
        return mHashNodeDB;
    }

    leveldb::DB* getHashNodeLDB ()
    {
        return mHashNodeLDB;
    }
    leveldb::DB* getEphemeralLDB ()
    {
        return mEphemeralLDB;
    }

    bool isShutdown ()
    {
        return mShutdown;
    }
    void setup ();
    void run ();
    void stop ();
    void sweep ();

private:
    void updateTables (bool);
    void startNewLedger ();
    bool loadOldLedger (const std::string&);

private:
    boost::asio::io_service mIOService;
    boost::asio::io_service mAuxService;
    boost::asio::io_service::work mIOWork;
    boost::asio::io_service::work mAuxWork;

    boost::recursive_mutex  mMasterLock;

    LocalCredentials   m_localCredentials;
    LedgerMaster       mLedgerMaster;
    InboundLedgers     m_inboundLedgers;
    TransactionMaster  mMasterTransaction;
    NetworkOPs         mNetOps;
    NodeCache          mTempNodeCache;
    HashedObjectStore  mHashedObjectStore;
    SLECache           mSLECache;
    SNTPClient         mSNTPClient;
    JobQueue           mJobQueue;
    TXQueue            mTxnQueue;
    OrderBookDB        mOrderBookDB;

    // VFALCO Clean stuff
    beast::ScopedPointer <IFeatures> mFeatures;
    beast::ScopedPointer <IFeeVote> mFeeVote;
    beast::ScopedPointer <ILoadFeeTrack> mFeeTrack;
    beast::ScopedPointer <IHashRouter> mHashRouter;
    beast::ScopedPointer <IValidations> mValidations;
    beast::ScopedPointer <IUniqueNodeList> mUNL;
    beast::ScopedPointer <IProofOfWorkFactory> mProofOfWorkFactory;
    beast::ScopedPointer <IPeers> mPeers;
    beast::ScopedPointer <ILoadManager> m_loadManager;
    // VFALCO End Clean stuff

    DatabaseCon* mRpcDB;
    DatabaseCon* mTxnDB;
    DatabaseCon* mLedgerDB;
    DatabaseCon* mWalletDB;
    DatabaseCon* mNetNodeDB;
    DatabaseCon* mPathFindDB;
    DatabaseCon* mHashNodeDB;

    // VFALCO TODO Wrap this in an interface
    leveldb::DB* mHashNodeLDB;
    leveldb::DB* mEphemeralLDB;

    PeerDoor*               mPeerDoor;
    RPCDoor*                mRPCDoor;
    WSDoor*                 mWSPublicDoor;
    WSDoor*                 mWSPrivateDoor;

    boost::asio::deadline_timer mSweepTimer;

    bool volatile mShutdown;
};

Application::Application ()
    : mIOService ((theConfig.NODE_SIZE >= 2) ? 2 : 1)
    , mIOWork (mIOService)
    , mAuxWork (mAuxService)
    , mNetOps (mIOService, &mLedgerMaster)
    , mTempNodeCache ("NodeCache", 16384, 90)
    , mHashedObjectStore (16384, 300)
    , mSLECache ("LedgerEntryCache", 4096, 120)
    , mSNTPClient (mAuxService)
    , mJobQueue (mIOService)
    // VFALCO New stuff
    , mFeatures (IFeatures::New (2 * 7 * 24 * 60 * 60, 200)) // two weeks, 200/256
    , mFeeVote (IFeeVote::New (10, 50 * SYSTEM_CURRENCY_PARTS, 12.5 * SYSTEM_CURRENCY_PARTS))
    , mFeeTrack (ILoadFeeTrack::New ())
    , mHashRouter (IHashRouter::New (IHashRouter::getDefaultHoldTime ()))
    , mValidations (IValidations::New ())
    , mUNL (IUniqueNodeList::New (mIOService))
    , mProofOfWorkFactory (IProofOfWorkFactory::New ())
    , mPeers (IPeers::New (mIOService))
    , m_loadManager (ILoadManager::New ())
    // VFALCO End new stuff
    // VFALCO TODO replace all NULL with nullptr
    , mRpcDB (NULL)
    , mTxnDB (NULL)
    , mLedgerDB (NULL)
    , mWalletDB (NULL) // VFALCO NOTE are all these 'NULL' ctor params necessary?
    , mNetNodeDB (NULL)
    , mPathFindDB (NULL)
    , mHashNodeDB (NULL)
    , mHashNodeLDB (NULL)
    , mEphemeralLDB (NULL)
    , mPeerDoor (NULL)
    , mRPCDoor (NULL)
    , mWSPublicDoor (NULL)
    , mWSPrivateDoor (NULL)
    , mSweepTimer (mAuxService)
    , mShutdown (false)
{
    // VFALCO TODO remove these once the call is thread safe.
    HashMaps::getInstance ().initializeNonce <size_t> ();
}

// VFALCO TODO Tidy these up into some class with accessors.
//
extern const char* RpcDBInit[], *TxnDBInit[], *LedgerDBInit[], *WalletDBInit[], *HashNodeDBInit[],
       *NetNodeDBInit[], *PathFindDBInit[];
extern int RpcDBCount, TxnDBCount, LedgerDBCount, WalletDBCount, HashNodeDBCount,
       NetNodeDBCount, PathFindDBCount;

void Application::stop ()
{
    WriteLog (lsINFO, Application) << "Received shutdown request";
    StopSustain ();
    mShutdown = true;
    mIOService.stop ();
    mHashedObjectStore.waitWrite ();
    mValidations->flush ();
    mAuxService.stop ();
    mJobQueue.shutdown ();

    delete mHashNodeLDB;
    mHashNodeLDB = NULL;

    delete mEphemeralLDB;
    mEphemeralLDB = NULL;

    WriteLog (lsINFO, Application) << "Stopped: " << mIOService.stopped ();
}

static void InitDB (DatabaseCon** dbCon, const char* fileName, const char* dbInit[], int dbCount)
{
    *dbCon = new DatabaseCon (fileName, dbInit, dbCount);
}

#ifdef SIGINT
void sigIntHandler (int)
{
    doShutdown = true;
}
#endif

// VFALCO TODO Figure this out it looks like the wrong tool
static void runAux (boost::asio::io_service& svc)
{
    setCallingThreadName ("aux");
    svc.run ();
}

static void runIO (boost::asio::io_service& io)
{
    setCallingThreadName ("io");
    io.run ();
}

// VFALCO TODO Break this function up into many small initialization segments.
//             Or better yet refactor these initializations into RAII classes
//             which are members of the Application object.
//
void Application::setup ()
{
    // VFALCO NOTE: 0 means use heuristics to determine the thread count.
    mJobQueue.setThreadCount (0, theConfig.RUN_STANDALONE);

    mSweepTimer.expires_from_now (boost::posix_time::seconds (10));
    mSweepTimer.async_wait (boost::bind (&Application::sweep, this));

    m_loadManager->startThread ();

#ifndef WIN32
#ifdef SIGINT

    if (!theConfig.RUN_STANDALONE)
    {
        struct sigaction sa;
        memset (&sa, 0, sizeof (sa));
        sa.sa_handler = sigIntHandler;
        sigaction (SIGINT, &sa, NULL);
    }

#endif
#endif

    assert (mTxnDB == NULL);

    if (!theConfig.DEBUG_LOGFILE.empty ())
    {
        // Let DEBUG messages go to the file but only WARNING or higher to regular output (unless verbose)
        Log::setLogFile (theConfig.DEBUG_LOGFILE);

        if (Log::getMinSeverity () > lsDEBUG)
            LogPartition::setSeverity (lsDEBUG);
    }

    boost::thread (boost::bind (runAux, boost::ref (mAuxService))).detach ();

    if (!theConfig.RUN_STANDALONE)
        mSNTPClient.init (theConfig.SNTP_SERVERS);

    //
    // Construct databases.
    //
    boost::thread t1 (boost::bind (&InitDB, &mRpcDB, "rpc.db", RpcDBInit, RpcDBCount));
    boost::thread t2 (boost::bind (&InitDB, &mTxnDB, "transaction.db", TxnDBInit, TxnDBCount));
    boost::thread t3 (boost::bind (&InitDB, &mLedgerDB, "ledger.db", LedgerDBInit, LedgerDBCount));
    t1.join ();
    t2.join ();
    t3.join ();

    boost::thread t4 (boost::bind (&InitDB, &mWalletDB, "wallet.db", WalletDBInit, WalletDBCount));
    boost::thread t6 (boost::bind (&InitDB, &mNetNodeDB, "netnode.db", NetNodeDBInit, NetNodeDBCount));
    boost::thread t7 (boost::bind (&InitDB, &mPathFindDB, "pathfind.db", PathFindDBInit, PathFindDBCount));
    t4.join ();
    t6.join ();
    t7.join ();

    leveldb::Options options;
    options.create_if_missing = true;
    options.block_cache = leveldb::NewLRUCache (theConfig.getSize (siHashNodeDBCache) * 1024 * 1024);

    if (theConfig.NODE_SIZE >= 2)
        options.filter_policy = leveldb::NewBloomFilterPolicy (10);

    if (theConfig.LDB_IMPORT)
        options.write_buffer_size = 32 << 20;

    if (mHashedObjectStore.isLevelDB ())
    {
        WriteLog (lsINFO, Application) << "LevelDB used for nodes";
        leveldb::Status status = leveldb::DB::Open (options, (theConfig.DATA_DIR / "hashnode").string (), &mHashNodeLDB);

        if (!status.ok () || !mHashNodeLDB)
        {
            WriteLog (lsFATAL, Application) << "Unable to open/create hash node db: "
                                            << (theConfig.DATA_DIR / "hashnode").string ()
                                            << " " << status.ToString ();
            StopSustain ();
            exit (3);
        }
    }
    else
    {
        WriteLog (lsINFO, Application) << "SQLite used for nodes";
        boost::thread t5 (boost::bind (&InitDB, &mHashNodeDB, "hashnode.db", HashNodeDBInit, HashNodeDBCount));
        t5.join ();
    }

    if (!theConfig.LDB_EPHEMERAL.empty ())
    {
        leveldb::Status status = leveldb::DB::Open (options, theConfig.LDB_EPHEMERAL, &mEphemeralLDB);

        if (!status.ok () || !mEphemeralLDB)
        {
            WriteLog (lsFATAL, Application) << "Unable to open/create epehemeral db: "
                                            << theConfig.LDB_EPHEMERAL << " " << status.ToString ();
            StopSustain ();
            exit (3);
        }
    }

    mTxnDB->getDB ()->setupCheckpointing (&mJobQueue);
    mLedgerDB->getDB ()->setupCheckpointing (&mJobQueue);

    if (!theConfig.RUN_STANDALONE)
        updateTables (theConfig.LDB_IMPORT);

    mFeatures->addInitialFeatures ();

    if (theConfig.START_UP == Config::FRESH)
    {
        WriteLog (lsINFO, Application) << "Starting new Ledger";

        startNewLedger ();
    }
    else if (theConfig.START_UP == Config::LOAD)
    {
        WriteLog (lsINFO, Application) << "Loading specified Ledger";

        if (!loadOldLedger (theConfig.START_LEDGER))
        {
            theApp->stop ();
            exit (-1);
        }
    }
    else if (theConfig.START_UP == Config::NETWORK)
    {
        // This should probably become the default once we have a stable network
        if (!theConfig.RUN_STANDALONE)
            mNetOps.needNetworkLedger ();

        startNewLedger ();
    }
    else
        startNewLedger ();

    mOrderBookDB.setup (theApp->getLedgerMaster ().getCurrentLedger ());

    //
    // Begin validation and ip maintenance.
    // - LocalCredentials maintains local information: including identity and network connection persistence information.
    //
    m_localCredentials.start ();

    //
    // Set up UNL.
    //
    if (!theConfig.RUN_STANDALONE)
        getUNL ().nodeBootstrap ();

    mValidations->tune (theConfig.getSize (siValidationsSize), theConfig.getSize (siValidationsAge));
    mHashedObjectStore.tune (theConfig.getSize (siNodeCacheSize), theConfig.getSize (siNodeCacheAge));
    mLedgerMaster.tune (theConfig.getSize (siLedgerSize), theConfig.getSize (siLedgerAge));
    mSLECache.setTargetSize (theConfig.getSize (siSLECacheSize));
    mSLECache.setTargetAge (theConfig.getSize (siSLECacheAge));

    mLedgerMaster.setMinValidations (theConfig.VALIDATION_QUORUM);

    if (!mHashedObjectStore.isLevelDB ())
        theApp->getHashNodeDB ()->getDB ()->executeSQL (boost::str (boost::format ("PRAGMA cache_size=-%d;") %
                (theConfig.getSize (siHashNodeDBCache) * 1024)));

    theApp->getLedgerDB ()->getDB ()->executeSQL (boost::str (boost::format ("PRAGMA cache_size=-%d;") %
            (theConfig.getSize (siLgrDBCache) * 1024)));
    theApp->getTxnDB ()->getDB ()->executeSQL (boost::str (boost::format ("PRAGMA cache_size=-%d;") %
            (theConfig.getSize (siTxnDBCache) * 1024)));

    //
    // Allow peer connections.
    //
    if (!theConfig.RUN_STANDALONE)
    {
        try
        {
            mPeerDoor = new PeerDoor (mIOService);
        }
        catch (const std::exception& e)
        {
            // Must run as directed or exit.
            WriteLog (lsFATAL, Application) << boost::str (boost::format ("Can not open peer service: %s") % e.what ());

            exit (3);
        }
    }
    else
    {
        WriteLog (lsINFO, Application) << "Peer interface: disabled";
    }

    //
    // Allow RPC connections.
    //
    if (!theConfig.RPC_IP.empty () && theConfig.RPC_PORT)
    {
        try
        {
            mRPCDoor = new RPCDoor (mIOService);
        }
        catch (const std::exception& e)
        {
            // Must run as directed or exit.
            WriteLog (lsFATAL, Application) << boost::str (boost::format ("Can not open RPC service: %s") % e.what ());

            exit (3);
        }
    }
    else
    {
        WriteLog (lsINFO, Application) << "RPC interface: disabled";
    }

    //
    // Allow private WS connections.
    //
    if (!theConfig.WEBSOCKET_IP.empty () && theConfig.WEBSOCKET_PORT)
    {
        try
        {
            mWSPrivateDoor  = WSDoor::createWSDoor (theConfig.WEBSOCKET_IP, theConfig.WEBSOCKET_PORT, false);
        }
        catch (const std::exception& e)
        {
            // Must run as directed or exit.
            WriteLog (lsFATAL, Application) << boost::str (boost::format ("Can not open private websocket service: %s") % e.what ());

            exit (3);
        }
    }
    else
    {
        WriteLog (lsINFO, Application) << "WS private interface: disabled";
    }

    //
    // Allow public WS connections.
    //
    if (!theConfig.WEBSOCKET_PUBLIC_IP.empty () && theConfig.WEBSOCKET_PUBLIC_PORT)
    {
        try
        {
            mWSPublicDoor   = WSDoor::createWSDoor (theConfig.WEBSOCKET_PUBLIC_IP, theConfig.WEBSOCKET_PUBLIC_PORT, true);
        }
        catch (const std::exception& e)
        {
            // Must run as directed or exit.
            WriteLog (lsFATAL, Application) << boost::str (boost::format ("Can not open public websocket service: %s") % e.what ());

            exit (3);
        }
    }
    else
    {
        WriteLog (lsINFO, Application) << "WS public interface: disabled";
    }

    //
    // Begin connecting to network.
    //
    if (!theConfig.RUN_STANDALONE)
        mPeers->start ();

    if (theConfig.RUN_STANDALONE)
    {
        WriteLog (lsWARNING, Application) << "Running in standalone mode";

        mNetOps.setStandAlone ();
    }
    else
    {
        // VFALCO NOTE the state timer resets the deadlock detector.
        //
        mNetOps.setStateTimer ();
    }
}

void Application::run ()
{
    if (theConfig.NODE_SIZE >= 2)
    {
        boost::thread (boost::bind (runIO, boost::ref (mIOService))).detach ();
    }

    if (!theConfig.RUN_STANDALONE)
    {
        // VFALCO NOTE This seems unnecessary. If we properly refactor the load
        //             manager then the deadlock detector can just always be "armed"
        //
	    theApp->getLoadManager ().activateDeadlockDetector ();
    }

    mIOService.run (); // This blocks

    if (mWSPublicDoor)
        mWSPublicDoor->stop ();

    if (mWSPrivateDoor)
        mWSPrivateDoor->stop ();

    WriteLog (lsINFO, Application) << "Done.";
}

void Application::sweep ()
{
    boost::filesystem::space_info space = boost::filesystem::space (theConfig.DATA_DIR);

    // VFALCO TODO Give this magic constant a name and move it into a well documented header
    //
    if (space.available < (512 * 1024 * 1024))
    {
        WriteLog (lsFATAL, Application) << "Remaining free disk space is less than 512MB";
        theApp->stop ();
    }

    // VFALCO NOTE Does the order of calls matter?
    // VFALCO TODO fix the dependency inversion using an observer,
    //         have listeners register for "onSweep ()" notification.
    //
    mMasterTransaction.sweep ();
    mHashedObjectStore.sweep ();
    mLedgerMaster.sweep ();
    mTempNodeCache.sweep ();
    mValidations->sweep ();
    getInboundLedgers ().sweep ();
    mSLECache.sweep ();
    AcceptedLedger::sweep (); // VFALCO NOTE AcceptedLedger is/has a singleton?
    SHAMap::sweep (); // VFALCO NOTE SHAMap is/has a singleton?
    mNetOps.sweepFetchPack ();
    // VFALCO NOTE does the call to sweep() happen on another thread?
    mSweepTimer.expires_from_now (boost::posix_time::seconds (theConfig.getSize (siSweepInterval)));
    mSweepTimer.async_wait (boost::bind (&Application::sweep, this));
}

Application::~Application ()
{
    // VFALCO TODO Wrap these in ScopedPointer
    delete mTxnDB;
    delete mLedgerDB;
    delete mWalletDB;
    delete mHashNodeDB;
    delete mNetNodeDB;
    delete mPathFindDB;
    delete mHashNodeLDB;

    if (mEphemeralLDB != nullptr)
        delete mEphemeralLDB;
}

void Application::startNewLedger ()
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
        mLedgerMaster.pushLedger (firstLedger);

        Ledger::pointer secondLedger = boost::make_shared<Ledger> (true, boost::ref (*firstLedger));
        secondLedger->setClosed ();
        secondLedger->setAccepted ();
        mLedgerMaster.pushLedger (secondLedger, boost::make_shared<Ledger> (true, boost::ref (*secondLedger)), false);
        assert (!!secondLedger->getAccountState (rootAddress));
        mNetOps.setLastCloseTime (secondLedger->getCloseTimeNC ());
    }
}

bool Application::loadOldLedger (const std::string& l)
{
    try
    {
        Ledger::pointer loadLedger;

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
            loadLedger = Ledger::loadByIndex (boost::lexical_cast<uint32> (l));

        if (!loadLedger)
        {
            WriteLog (lsFATAL, Application) << "No Ledger found?" << std::endl;
            return false;
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

        mLedgerMaster.setLedgerRangePresent (loadLedger->getLedgerSeq (), loadLedger->getLedgerSeq ());

        Ledger::pointer openLedger = boost::make_shared<Ledger> (false, boost::ref (*loadLedger));
        mLedgerMaster.switchLedgers (loadLedger, openLedger);
        mNetOps.setLastCloseTime (loadLedger->getCloseTimeNC ());
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
    if (!theConfig.ELB_SUPPORT)
        return true;

    if (!theApp)
    {
        reason = "Server has not started";
        return false;
    }

    if (theApp->isShutdown ())
    {
        reason = "Server is shutting down";
        return false;
    }

    if (theApp->getOPs ().isNeedNetworkLedger ())
    {
        reason = "Not synchronized with network yet";
        return false;
    }

    if (theApp->getOPs ().getOperatingMode () < NetworkOPs::omSYNCING)
    {
        reason = "Not synchronized with network";
        return false;
    }

    if (theApp->getFeeTrack ().isLoaded ())
    {
        reason = "Too much load";
        return false;
    }

    if (theApp->getOPs ().isFeatureBlocked ())
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
    if (schemaHas (theApp->getTxnDB (), "AccountTransactions", 0, "TxnSeq"))
        return;

    Log (lsWARNING) << "Transaction sequence field is missing";

    Database* db = theApp->getTxnDB ()->getDB ();

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

void Application::updateTables (bool ldbImport)
{
    // perform any needed table updates
    assert (schemaHas (theApp->getTxnDB (), "AccountTransactions", 0, "TransID"));
    assert (!schemaHas (theApp->getTxnDB (), "AccountTransactions", 0, "foobar"));
    addTxnSeqField ();

    if (schemaHas (theApp->getTxnDB (), "AccountTransactions", 0, "PRIMARY"))
    {
        Log (lsFATAL) << "AccountTransactions database should not have a primary key";
        StopSustain ();
        exit (1);
    }

    if (theApp->getHashedObjectStore ().isLevelDB ())
    {
        boost::filesystem::path hashPath = theConfig.DATA_DIR / "hashnode.db";

        if (boost::filesystem::exists (hashPath))
        {
            if (theConfig.LDB_IMPORT)
            {
                Log (lsWARNING) << "Importing SQLite -> LevelDB";
                theApp->getHashedObjectStore ().import (hashPath.string ());
                Log (lsWARNING) << "Remove or remname the hashnode.db file";
            }
            else
            {
                Log (lsWARNING) << "SQLite hashnode database exists. Please either remove or import";
                Log (lsWARNING) << "To import, start with the '--import' option. Otherwise, remove hashnode.db";
                StopSustain ();
                exit (1);
            }
        }
    }
}

IApplication* IApplication::New ()
{
    return new Application;
}
