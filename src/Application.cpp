
#include <iostream>

//#include <boost/log/trivial.hpp>

#include "../database/SqliteDatabase.h"

#include "Application.h"
#include "Config.h"
#include "PeerDoor.h"
#include "RPCDoor.h"
#include "BitcoinUtil.h"
#include "key.h"

Application* theApp=NULL;

/*
What needs to happen:
	Listen for connections
	Try to maintain the right number of connections
	Process messages from peers
	Process messages from RPC
	Periodically publish a new ledger
	Save the various pieces of data 

*/

DatabaseCon::DatabaseCon(const std::string& name, const char *initStrings[], int initCount)
{
	std::string path=strprintf("%s%s", theConfig.DATA_DIR.c_str(), name.c_str());
	mDatabase=new SqliteDatabase(path.c_str());
	mDatabase->connect();
	for(int i=0; i<initCount; i++)
		mDatabase->executeSQL(initStrings[i], true);
}

DatabaseCon::~DatabaseCon()
{
	mDatabase->disconnect();
	delete mDatabase;
}

Application::Application() :
	mUNL(mIOService),
	mTxnDB(NULL), mLedgerDB(NULL), mWalletDB(NULL), mHashNodeDB(NULL), mNetNodeDB(NULL),
	mPeerDoor(NULL), mRPCDoor(NULL)
{
	theConfig.load();
}

extern const char *TxnDBInit[], *LedgerDBInit[], *WalletDBInit[], *HashNodeDBInit[], *NetNodeDBInit[];
extern int TxnDBCount, LedgerDBCount, WalletDBCount, HashNodeDBCount, NetNodeDBCount;

void Application::stop()
{
	mIOService.stop();

    std::cerr << "Stopped: " << mIOService.stopped() << std::endl;
}

void Application::run()
{
	assert(mTxnDB==NULL);

	//
	// Construct databases.
	//
	mTxnDB=new DatabaseCon("transaction.db", TxnDBInit, TxnDBCount);
	mLedgerDB=new DatabaseCon("ledger.db", LedgerDBInit, LedgerDBCount);
	mWalletDB=new DatabaseCon("wallet.db", WalletDBInit, WalletDBCount);
	mHashNodeDB=new DatabaseCon("hashnode.db", HashNodeDBInit, HashNodeDBCount);
	mNetNodeDB=new DatabaseCon("netnode.db", NetNodeDBInit, NetNodeDBCount);

	//
	// Begin validation and ip maintenance.
	//

	mWallet.start();

	//
	// Allow peer connections.
	//
	if(theConfig.PEER_PORT)
	{
		mPeerDoor=new PeerDoor(mIOService);
	}//else BOOST_LOG_TRIVIAL(info) << "No Peer Port set. Not listening for connections.";

	//
	// Allow RPC connections.
	//
	if(theConfig.RPC_PORT)
	{
		mRPCDoor=new RPCDoor(mIOService);
	}//else BOOST_LOG_TRIVIAL(info) << "No RPC Port set. Not listening for commands.";

	mConnectionPool.connectToNetwork(mKnownNodes, mIOService);
	mTimingService.start(mIOService);

	std::cout << "Before Run." << std::endl;

	// Temporary root account will be ["This is my payphrase."]:0
	NewcoinAddress rootFamilySeed;		// Hold the 128 password.
	NewcoinAddress rootFamilyGenerator;	// Hold the generator.
	NewcoinAddress rootAddress;

	rootFamilySeed.setFamilySeed(CKey::PassPhraseToKey("This is my payphrase"));
	rootFamilyGenerator.setFamilyGenerator(rootFamilySeed);
	rootAddress.setAccountPublic(rootFamilyGenerator, 0);

	Ledger::pointer firstLedger(new Ledger(rootAddress, 100000000));
	firstLedger->setClosed();
	firstLedger->setAccepted();
	mMasterLedger.pushLedger(firstLedger);
	Ledger::pointer secondLedger=firstLedger->closeLedger(time(NULL));
	mMasterLedger.pushLedger(secondLedger);
	mMasterLedger.setSynced();
	// temporary

	mWallet.load();
//	mWallet.syncToLedger(true, &(*secondLedger));

	// temporary
	mIOService.run(); // This blocks

	//BOOST_LOG_TRIVIAL(info) << "Done.";
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
// vim:ts=4
