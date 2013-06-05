
#include "Application.h"

#ifdef USE_LEVELDB
#include "leveldb/cache.h"
#include "leveldb/filter_policy.h"
#endif

#include "AcceptedLedger.h"
#include "Config.h"
#include "PeerDoor.h"
#include "RPCDoor.h"

#include "../database/SqliteDatabase.h"

#include <iostream>

#include <boost/bind.hpp>
#include <boost/filesystem.hpp>
#include <boost/thread.hpp>

SETUP_LOG (Application)

// VFALCO: TODO, fix/clean this, it might have broken with the Log changes
LogPartition AutoSocketPartition("AutoSocket");
Application* theApp = NULL;

int DatabaseCon::sCount = 0;

DatabaseCon::DatabaseCon(const std::string& strName, const char *initStrings[], int initCount)
{
	++sCount;
	boost::filesystem::path	pPath	= (theConfig.RUN_STANDALONE && (theConfig.START_UP != Config::LOAD))
										? ""								// Use temporary files.
										: (theConfig.DATA_DIR / strName);		// Use regular db files.

	mDatabase = new SqliteDatabase(pPath.string().c_str());
	mDatabase->connect();
	for(int i = 0; i < initCount; ++i)
		mDatabase->executeSQL(initStrings[i], true);
}

DatabaseCon::~DatabaseCon()
{
	mDatabase->disconnect();
	delete mDatabase;
}

Application::Application() :
	mIOService((theConfig.NODE_SIZE >= 2) ? 2 : 1),
	mIOWork(mIOService), mAuxWork(mAuxService), mUNL(mIOService), mNetOps(mIOService, &mLedgerMaster),
	mTempNodeCache("NodeCache", 16384, 90), mHashedObjectStore(16384, 300), mSLECache("LedgerEntryCache", 4096, 120),
	mSNTPClient(mAuxService), mJobQueue(mIOService), mFeeTrack(),

	mFeeVote(10, 50 * SYSTEM_CURRENCY_PARTS, 12.5 * SYSTEM_CURRENCY_PARTS),
	mFeatureTable(2 * 7 * 24 * 60 * 60, 200), // two weeks, 200/256

	mRpcDB(NULL), mTxnDB(NULL), mLedgerDB(NULL), mWalletDB(NULL),
	mNetNodeDB(NULL), mPathFindDB(NULL), mHashNodeDB(NULL),
#ifdef USE_LEVELDB
	mHashNodeLDB(NULL),
#endif
	mConnectionPool(mIOService), mPeerDoor(NULL), mRPCDoor(NULL), mWSPublicDoor(NULL), mWSPrivateDoor(NULL),
	mSweepTimer(mAuxService), mShutdown(false)
{
	getRand(mNonce256.begin(), mNonce256.size());
	getRand(reinterpret_cast<unsigned char *>(&mNonceST), sizeof(mNonceST));
}

extern const char *RpcDBInit[], *TxnDBInit[], *LedgerDBInit[], *WalletDBInit[], *HashNodeDBInit[],
	*NetNodeDBInit[], *PathFindDBInit[];
extern int RpcDBCount, TxnDBCount, LedgerDBCount, WalletDBCount, HashNodeDBCount,
	NetNodeDBCount, PathFindDBCount;
bool Instance::running = true;

void Application::stop()
{
	WriteLog (lsINFO, Application) << "Received shutdown request";
	StopSustain();
	mShutdown = true;
	mIOService.stop();
	mHashedObjectStore.waitWrite();
	mValidations.flush();
	mAuxService.stop();
	mJobQueue.shutdown();

#ifdef USE_LEVELDB
	delete mHashNodeLDB;
	mHashNodeLDB = NULL;
#endif

	WriteLog (lsINFO, Application) << "Stopped: " << mIOService.stopped();
	Instance::shutdown();
}

static void InitDB(DatabaseCon** dbCon, const char *fileName, const char *dbInit[], int dbCount)
{
	*dbCon = new DatabaseCon(fileName, dbInit, dbCount);
}

volatile bool doShutdown = false;

#ifdef SIGINT
void sigIntHandler(int)
{
	doShutdown = true;
}
#endif

static void runAux(boost::asio::io_service& svc)
{
	setCallingThreadName("aux");
	svc.run();
}

static void runIO(boost::asio::io_service& io)
{
	setCallingThreadName("io");
	io.run();
}

void Application::setup()
{
	mJobQueue.setThreadCount();
	mSweepTimer.expires_from_now(boost::posix_time::seconds(10));
	mSweepTimer.async_wait(boost::bind(&Application::sweep, this));
	mLoadMgr.init();

#ifndef WIN32
#ifdef SIGINT
	if (!theConfig.RUN_STANDALONE)
	{
		struct sigaction sa;
		memset(&sa, 0, sizeof(sa));
		sa.sa_handler = sigIntHandler;
		sigaction(SIGINT, &sa, NULL);
	}
#endif
#endif

	assert(mTxnDB == NULL);
	if (!theConfig.DEBUG_LOGFILE.empty())
	{ // Let DEBUG messages go to the file but only WARNING or higher to regular output (unless verbose)
		Log::setLogFile(theConfig.DEBUG_LOGFILE);
		if (Log::getMinSeverity() > lsDEBUG)
			LogPartition::setSeverity(lsDEBUG);
	}

	boost::thread(boost::bind(runAux, boost::ref(mAuxService))).detach();

	if (!theConfig.RUN_STANDALONE)
		mSNTPClient.init(theConfig.SNTP_SERVERS);

	//
	// Construct databases.
	//
	boost::thread t1(boost::bind(&InitDB, &mRpcDB, "rpc.db", RpcDBInit, RpcDBCount));
	boost::thread t2(boost::bind(&InitDB, &mTxnDB, "transaction.db", TxnDBInit, TxnDBCount));
	boost::thread t3(boost::bind(&InitDB, &mLedgerDB, "ledger.db", LedgerDBInit, LedgerDBCount));
	t1.join(); t2.join(); t3.join();

	boost::thread t4(boost::bind(&InitDB, &mWalletDB, "wallet.db", WalletDBInit, WalletDBCount));
	boost::thread t6(boost::bind(&InitDB, &mNetNodeDB, "netnode.db", NetNodeDBInit, NetNodeDBCount));
	boost::thread t7(boost::bind(&InitDB, &mPathFindDB, "pathfind.db", PathFindDBInit, PathFindDBCount));
	t4.join(); t6.join(); t7.join();

#ifdef USE_LEVELDB
	if (mHashedObjectStore.isLevelDB())
	{
		WriteLog (lsINFO, Application) << "LevelDB used for nodes";
		leveldb::Options options;
		options.create_if_missing = true;
		options.block_cache = leveldb::NewLRUCache(theConfig.getSize(siHashNodeDBCache) * 1024 * 1024);
		if (theConfig.NODE_SIZE >= 2)
			options.filter_policy = leveldb::NewBloomFilterPolicy(10);
		if (theConfig.LDB_IMPORT)
			options.write_buffer_size = 32 << 20;
		leveldb::Status status = leveldb::DB::Open(options, (theConfig.DATA_DIR / "hashnode").string(), &mHashNodeLDB);
		if (!status.ok() || !mHashNodeLDB)
		{
			WriteLog (lsFATAL, Application) << "Unable to open/create hash node db: "
				<< (theConfig.DATA_DIR / "hashnode").string()
				<< " " << status.ToString();
			StopSustain();
			exit(3);
		}
	}
	else
#endif
	{
		WriteLog (lsINFO, Application) << "SQLite used for nodes";
		boost::thread t5(boost::bind(&InitDB, &mHashNodeDB, "hashnode.db", HashNodeDBInit, HashNodeDBCount));
		t5.join();
	}

	mTxnDB->getDB()->setupCheckpointing(&mJobQueue);
	mLedgerDB->getDB()->setupCheckpointing(&mJobQueue);

	if (!theConfig.RUN_STANDALONE)
		updateTables(theConfig.LDB_IMPORT);

	if (theConfig.START_UP == Config::FRESH)
	{
		WriteLog (lsINFO, Application) << "Starting new Ledger";

		startNewLedger();
	}
	else if (theConfig.START_UP == Config::LOAD)
	{
		WriteLog (lsINFO, Application) << "Loading specified Ledger";

		if (!loadOldLedger(theConfig.START_LEDGER))
		{
			theApp->stop();
			exit(-1);
		}
	}
	else if (theConfig.START_UP == Config::NETWORK)
	{ // This should probably become the default once we have a stable network
		if (!theConfig.RUN_STANDALONE)
			mNetOps.needNetworkLedger();
		startNewLedger();
	}
	else
		startNewLedger();

	mOrderBookDB.setup(theApp->getLedgerMaster().getCurrentLedger());

	//
	// Begin validation and ip maintenance.
	// - Wallet maintains local information: including identity and network connection persistence information.
	//
	mWallet.start();

	//
	// Set up UNL.
	//
	if (!theConfig.RUN_STANDALONE)
		getUNL().nodeBootstrap();

	mValidations.tune(theConfig.getSize(siValidationsSize), theConfig.getSize(siValidationsAge));
	mHashedObjectStore.tune(theConfig.getSize(siNodeCacheSize), theConfig.getSize(siNodeCacheAge));
	mLedgerMaster.tune(theConfig.getSize(siLedgerSize), theConfig.getSize(siLedgerAge));
	mSLECache.setTargetSize(theConfig.getSize(siSLECacheSize));
	mSLECache.setTargetAge(theConfig.getSize(siSLECacheAge));

	mLedgerMaster.setMinValidations(theConfig.VALIDATION_QUORUM);

#ifdef USE_LEVELDB
	if (!mHashedObjectStore.isLevelDB())
#endif
		theApp->getHashNodeDB()->getDB()->executeSQL(boost::str(boost::format("PRAGMA cache_size=-%d;") %
			(theConfig.getSize(siHashNodeDBCache) * 1024)));

	theApp->getLedgerDB()->getDB()->executeSQL(boost::str(boost::format("PRAGMA cache_size=-%d;") %
		(theConfig.getSize(siTxnDBCache) * 1024)));
	theApp->getTxnDB()->getDB()->executeSQL(boost::str(boost::format("PRAGMA cache_size=-%d;") %
		(theConfig.getSize(siLgrDBCache) * 1024)));

	//
	// Allow peer connections.
	//
	if (!theConfig.RUN_STANDALONE)
	{
		try
		{
			mPeerDoor = new PeerDoor(mIOService);
		}
		catch (const std::exception& e)
		{
			// Must run as directed or exit.
			WriteLog (lsFATAL, Application) << boost::str(boost::format("Can not open peer service: %s") % e.what());

			exit(3);
		}
	}
	else
	{
		WriteLog (lsINFO, Application) << "Peer interface: disabled";
	}

	//
	// Allow RPC connections.
	//
	if (!theConfig.RPC_IP.empty() && theConfig.RPC_PORT)
	{
		try
		{
			mRPCDoor = new RPCDoor(mIOService);
		}
		catch (const std::exception& e)
		{
			// Must run as directed or exit.
			WriteLog (lsFATAL, Application) << boost::str(boost::format("Can not open RPC service: %s") % e.what());

			exit(3);
		}
	}
	else
	{
		WriteLog (lsINFO, Application) << "RPC interface: disabled";
	}

	//
	// Allow private WS connections.
	//
	if (!theConfig.WEBSOCKET_IP.empty() && theConfig.WEBSOCKET_PORT)
	{
		try
		{
			mWSPrivateDoor	= WSDoor::createWSDoor(theConfig.WEBSOCKET_IP, theConfig.WEBSOCKET_PORT, false);
		}
		catch (const std::exception& e)
		{
			// Must run as directed or exit.
			WriteLog (lsFATAL, Application) << boost::str(boost::format("Can not open private websocket service: %s") % e.what());

			exit(3);
		}
	}
	else
	{
		WriteLog (lsINFO, Application) << "WS private interface: disabled";
	}

	//
	// Allow public WS connections.
	//
	if (!theConfig.WEBSOCKET_PUBLIC_IP.empty() && theConfig.WEBSOCKET_PUBLIC_PORT)
	{
		try
		{
			mWSPublicDoor	= WSDoor::createWSDoor(theConfig.WEBSOCKET_PUBLIC_IP, theConfig.WEBSOCKET_PUBLIC_PORT, true);
		}
		catch (const std::exception& e)
		{
			// Must run as directed or exit.
			WriteLog (lsFATAL, Application) << boost::str(boost::format("Can not open public websocket service: %s") % e.what());

			exit(3);
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
		mConnectionPool.start();


	if (theConfig.RUN_STANDALONE)
	{
		WriteLog (lsWARNING, Application) << "Running in standalone mode";

		mNetOps.setStandAlone();
	}
	else
		mNetOps.setStateTimer();
}

void Application::run()
{
	if (theConfig.NODE_SIZE >= 2)
	{
		boost::thread(boost::bind(runIO, boost::ref(mIOService))).detach();
	}

	theApp->getLoadManager().arm();
	mIOService.run(); // This blocks

	if (mWSPublicDoor)
		mWSPublicDoor->stop();

	if (mWSPrivateDoor)
		mWSPrivateDoor->stop();

	WriteLog (lsINFO, Application) << "Done.";
}

void Application::sweep()
{

	boost::filesystem::space_info space = boost::filesystem::space(theConfig.DATA_DIR);
	if (space.available < (512 * 1024 * 1024))
	{
		WriteLog (lsFATAL, Application) << "Remaining free disk space is less than 512MB";
		theApp->stop();
	}

	mMasterTransaction.sweep();
	mHashedObjectStore.sweep();
	mLedgerMaster.sweep();
	mTempNodeCache.sweep();
	mValidations.sweep();
	getMasterLedgerAcquire().sweep();
	mSLECache.sweep();
	AcceptedLedger::sweep();
	SHAMap::sweep();
	mNetOps.sweepFetchPack();
	mSweepTimer.expires_from_now(boost::posix_time::seconds(theConfig.getSize(siSweepInterval)));
	mSweepTimer.async_wait(boost::bind(&Application::sweep, this));
}

Application::~Application()
{
	delete mTxnDB;
	delete mLedgerDB;
	delete mWalletDB;
	delete mHashNodeDB;
	delete mNetNodeDB;
	delete mPathFindDB;
#ifdef USE_LEVELDB
	delete mHashNodeLDB;
#endif
}

void Application::startNewLedger()
{
	// New stuff.
	RippleAddress	rootSeedMaster		= RippleAddress::createSeedGeneric("masterpassphrase");
	RippleAddress	rootGeneratorMaster	= RippleAddress::createGeneratorPublic(rootSeedMaster);
	RippleAddress	rootAddress			= RippleAddress::createAccountPublic(rootGeneratorMaster, 0);

	// Print enough information to be able to claim root account.
	WriteLog (lsINFO, Application) << "Root master seed: " << rootSeedMaster.humanSeed();
	WriteLog (lsINFO, Application) << "Root account: " << rootAddress.humanAccountID();

	{
		Ledger::pointer firstLedger = boost::make_shared<Ledger>(rootAddress, SYSTEM_CURRENCY_START);
		assert(!!firstLedger->getAccountState(rootAddress));
		firstLedger->updateHash();
		firstLedger->setClosed();
		firstLedger->setAccepted();
		mLedgerMaster.pushLedger(firstLedger);

		Ledger::pointer secondLedger = boost::make_shared<Ledger>(true, boost::ref(*firstLedger));
		secondLedger->setClosed();
		secondLedger->setAccepted();
		mLedgerMaster.pushLedger(secondLedger, boost::make_shared<Ledger>(true, boost::ref(*secondLedger)), false);
		assert(!!secondLedger->getAccountState(rootAddress));
		mNetOps.setLastCloseTime(secondLedger->getCloseTimeNC());
	}
}

bool Application::loadOldLedger(const std::string& l)
{
	try
	{
		Ledger::pointer loadLedger;
		if (l.empty() || (l == "latest"))
			loadLedger = Ledger::getLastFullLedger();
		else if (l.length() == 64)
		{ // by hash
			uint256 hash;
			hash.SetHex(l);
			loadLedger = Ledger::loadByHash(hash);
		}
		else // assume by sequence
			loadLedger = Ledger::loadByIndex(boost::lexical_cast<uint32>(l));

		if (!loadLedger)
		{
			WriteLog (lsFATAL, Application) << "No Ledger found?" << std::endl;
			return false;
		}
		loadLedger->setClosed();

		WriteLog (lsINFO, Application) << "Loading ledger " << loadLedger->getHash() << " seq:" << loadLedger->getLedgerSeq();

		if (loadLedger->getAccountHash().isZero())
		{
			WriteLog (lsFATAL, Application) << "Ledger is empty.";
			assert(false);
			return false;
		}

		if (!loadLedger->walkLedger())
		{
			WriteLog (lsFATAL, Application) << "Ledger is missing nodes.";
			return false;
		}

		if (!loadLedger->assertSane())
		{
			WriteLog (lsFATAL, Application) << "Ledger is not sane.";
			return false;
		}
		mLedgerMaster.setLedgerRangePresent(loadLedger->getLedgerSeq(), loadLedger->getLedgerSeq());

		Ledger::pointer openLedger = boost::make_shared<Ledger>(false, boost::ref(*loadLedger));
		mLedgerMaster.switchLedgers(loadLedger, openLedger);
		mNetOps.setLastCloseTime(loadLedger->getCloseTimeNC());
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

bool serverOkay(std::string& reason)
{
	if (!theConfig.ELB_SUPPORT)
		return true;

	if (!theApp)
	{
		reason = "Server has not started";
		return false;
	}

	if (theApp->isShutdown())
	{
		reason = "Server is shutting down";
		return false;
	}

	if (theApp->getOPs().isNeedNetworkLedger())
	{
		reason = "Not synchronized with network yet";
		return false;
	}

	if (theApp->getOPs().getOperatingMode() < NetworkOPs::omSYNCING)
	{
		reason = "Not synchronized with network";
		return false;
	}

	if (theApp->getFeeTrack().isLoaded())
	{
		reason = "Too much load";
		return false;
	}

	return true;
}

// vim:ts=4
