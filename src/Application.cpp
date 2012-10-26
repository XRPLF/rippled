
#include "Application.h"
#include "Config.h"
#include "PeerDoor.h"
#include "RPCDoor.h"
#include "BitcoinUtil.h"
#include "key.h"
#include "utils.h"
#include "TaggedCache.h"
#include "Log.h"

#include "../database/SqliteDatabase.h"

#include <iostream>

#include <boost/bind.hpp>
#include <boost/filesystem.hpp>
#include <boost/thread.hpp>

SETUP_LOG();

Application* theApp = NULL;

DatabaseCon::DatabaseCon(const std::string& strName, const char *initStrings[], int initCount)
{
	boost::filesystem::path	pPath	= theConfig.DATA_DIR / strName;

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
	mIOWork(mIOService), mAuxWork(mAuxService), mUNL(mIOService),
	mNetOps(mIOService, &mMasterLedger), mTempNodeCache(16384, 90), mHashedObjectStore(16384, 300),
	mSNTPClient(mAuxService), mRpcDB(NULL), mTxnDB(NULL), mLedgerDB(NULL), mWalletDB(NULL),
	mHashNodeDB(NULL), mNetNodeDB(NULL),
	mConnectionPool(mIOService), mPeerDoor(NULL), mRPCDoor(NULL)
{
	RAND_bytes(mNonce256.begin(), mNonce256.size());
	RAND_bytes(reinterpret_cast<unsigned char *>(&mNonceST), sizeof(mNonceST));
}

extern const char *RpcDBInit[], *TxnDBInit[], *LedgerDBInit[], *WalletDBInit[], *HashNodeDBInit[], *NetNodeDBInit[];
extern int RpcDBCount, TxnDBCount, LedgerDBCount, WalletDBCount, HashNodeDBCount, NetNodeDBCount;

void Application::stop()
{
	mIOService.stop();
	mHashedObjectStore.bulkWrite();
	mValidations.flush();
	mAuxService.stop();

	cLog(lsINFO) << "Stopped: " << mIOService.stopped();
}

static void InitDB(DatabaseCon** dbCon, const char *fileName, const char *dbInit[], int dbCount)
{
	*dbCon = new DatabaseCon(fileName, dbInit, dbCount);
}

void Application::run()
{
	assert(mTxnDB == NULL);
	if (!theConfig.DEBUG_LOGFILE.empty())
	{ // Let DEBUG messages go to the file but only WARNING or higher to regular output
		Log::setLogFile(theConfig.DEBUG_LOGFILE);
		LogPartition::setSeverity(lsDEBUG);
	}

	boost::thread auxThread(boost::bind(&boost::asio::io_service::run, &mAuxService));
	auxThread.detach();


	if (!theConfig.RUN_STANDALONE)
		mSNTPClient.init(theConfig.SNTP_SERVERS);

	//
	// Construct databases.
	//
	boost::thread t1(boost::bind(&InitDB, &mRpcDB, "rpc.db", RpcDBInit, RpcDBCount));
	boost::thread t2(boost::bind(&InitDB, &mTxnDB, "transaction.db", TxnDBInit, TxnDBCount));
	boost::thread t3(boost::bind(&InitDB, &mLedgerDB, "ledger.db", LedgerDBInit, LedgerDBCount));
	boost::thread t4(boost::bind(&InitDB, &mWalletDB, "wallet.db", WalletDBInit, WalletDBCount));
	boost::thread t5(boost::bind(&InitDB, &mHashNodeDB, "hashnode.db", HashNodeDBInit, HashNodeDBCount));
	boost::thread t6(boost::bind(&InitDB, &mNetNodeDB, "netnode.db", NetNodeDBInit, NetNodeDBCount));
	t1.join(); t2.join(); t3.join(); t4.join(); t5.join(); t6.join();

	if (theConfig.START_UP == Config::FRESH)
	{
		cLog(lsINFO) << "Starting new Ledger";
		startNewLedger();
	}
	else if (theConfig.START_UP == Config::LOAD)
	{
		cLog(lsINFO) << "Loading Old Ledger";
		loadOldLedger();
	}
	else if (theConfig.START_UP == Config::NETWORK)
	{ // This should probably become the default once we have a stable network
		if (!theConfig.RUN_STANDALONE)
			mNetOps.needNetworkLedger();
		startNewLedger();
	}
	else
		startNewLedger();

	if (theConfig.FULL_HISTORY && (theConfig.START_UP != Config::LOAD))
	{
		Ledger::pointer ledger = Ledger::getLastFullLedger();
		if (ledger)
			mMasterLedger.setLedgerRangePresent(0, ledger->getLedgerSeq());
	}

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


	//
	// Allow peer connections.
	//
	if (!theConfig.RUN_STANDALONE && !theConfig.PEER_IP.empty() && theConfig.PEER_PORT)
	{
		mPeerDoor = new PeerDoor(mIOService);
	}
	else
	{
		std::cerr << "Peer interface: disabled" << std::endl;
	}

	//
	// Allow RPC connections.
	//
	if (!theConfig.RPC_IP.empty() && theConfig.RPC_PORT)
	{
		mRPCDoor = new RPCDoor(mIOService);
	}
	else
	{
		std::cerr << "RPC interface: disabled" << std::endl;
	}

	mWSDoor		= WSDoor::createWSDoor();

	//
	// Begin connecting to network.
	//
	if (!theConfig.RUN_STANDALONE)
		mConnectionPool.start();


	if (theConfig.RUN_STANDALONE)
	{
		cLog(lsWARNING) << "Running in standalone mode";
		mNetOps.setStandAlone();
	}
	else
		mNetOps.setStateTimer();

	mIOService.run(); // This blocks

	mWSDoor->stop();

	std::cout << "Done." << std::endl;
}

Application::~Application()
{
	delete mTxnDB;
	delete mLedgerDB;
	delete mWalletDB;
	delete mHashNodeDB;
	delete mNetNodeDB;
}

void Application::startNewLedger()
{
	// New stuff.
	RippleAddress	rootSeedMaster		= RippleAddress::createSeedGeneric("masterpassphrase");
	RippleAddress	rootGeneratorMaster	= RippleAddress::createGeneratorPublic(rootSeedMaster);
	RippleAddress	rootAddress			= RippleAddress::createAccountPublic(rootGeneratorMaster, 0);

	// Print enough information to be able to claim root account.
	cLog(lsINFO) << "Root master seed: " << rootSeedMaster.humanSeed();
	cLog(lsINFO) << "Root account: " << rootAddress.humanAccountID();

	{
		Ledger::pointer firstLedger = boost::make_shared<Ledger>(rootAddress, SYSTEM_CURRENCY_START);
		assert(!!firstLedger->getAccountState(rootAddress));
		firstLedger->updateHash();
		firstLedger->setClosed();
		firstLedger->setAccepted();
		mMasterLedger.pushLedger(firstLedger);

		Ledger::pointer secondLedger = boost::make_shared<Ledger>(true, boost::ref(*firstLedger));
		secondLedger->setClosed();
		secondLedger->setAccepted();
		mMasterLedger.pushLedger(secondLedger, boost::make_shared<Ledger>(true, boost::ref(*secondLedger)), false);
		assert(!!secondLedger->getAccountState(rootAddress));
		mNetOps.setLastCloseTime(secondLedger->getCloseTimeNC());
	}
}

void Application::loadOldLedger()
{
	try
	{
		Ledger::pointer lastLedger = Ledger::getLastFullLedger();

		if (!lastLedger)
		{
			std::cout << "No Ledger found?" << std::endl;
			exit(-1);
		}
		lastLedger->setClosed();

		cLog(lsINFO) << "Loading ledger " << lastLedger->getHash() << " seq:" << lastLedger->getLedgerSeq();

		if (lastLedger->getAccountHash().isZero())
		{
			cLog(lsFATAL) << "Ledger is empty.";
			assert(false);
			exit(-1);
		}

		if (!lastLedger->walkLedger())
		{
			cLog(lsFATAL) << "Ledger is missing nodes.";
			exit(-1);
		}

		if (!lastLedger->assertSane())
		{
			cLog(lsFATAL) << "Ledger is not sane.";
			exit(-1);
		}
		mMasterLedger.setLedgerRangePresent(0, lastLedger->getLedgerSeq());

		Ledger::pointer openLedger = boost::make_shared<Ledger>(false, boost::ref(*lastLedger));
		mMasterLedger.switchLedgers(lastLedger, openLedger);
		mNetOps.setLastCloseTime(lastLedger->getCloseTimeNC());
	}
	catch (SHAMapMissingNode& mn)
	{
		cLog(lsFATAL) << "Cannot load ledger. " << mn;
		exit(-1);
	}
}
// vim:ts=4
