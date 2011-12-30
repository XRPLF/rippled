
#include <iostream>

//#include <boost/log/trivial.hpp>

#include "database/SqliteDatabase.h"

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

DatabaseCon::DatabaseCon(const std::string& name)
{
	std::string path=strprintf("%s%s", theConfig.DATA_DIR.c_str(), name.c_str());
	mDatabase=new SqliteDatabase(path.c_str());
	mDatabase->connect();
}

DatabaseCon::~DatabaseCon()
{
	mDatabase->disconnect();
	delete mDatabase;
}

Application::Application() :
	mTxnDB(NULL), mLedgerDB(NULL), mWalletDB(NULL), mHashNodeDB(NULL), mNetNodeDB(NULL),
	mPeerDoor(NULL), mRPCDoor(NULL)
{
	theConfig.load();

	uint160 rootFamily=mWallet.addFamily("This is my payphrase.", true);
	LocalAccount::pointer rootAccount=mWallet.getLocalAccount(rootFamily, 0);
	assert(!!rootAccount);
	uint160 rootAddress=rootAccount->getAddress();
	assert(!!rootAddress);

	Ledger::pointer firstLedger(new Ledger(rootAddress, 1000000));
	firstLedger->setClosed();
	firstLedger->setAccepted();
	mMasterLedger.pushLedger(firstLedger);
	
	Ledger::pointer secondLedger=firstLedger->closeLedger(time(NULL));
	mMasterLedger.pushLedger(secondLedger);
	mMasterLedger.setSynced();
}


void Application::run()
{
	mTxnDB=new DatabaseCon("transaction.db");
	mLedgerDB=new DatabaseCon("ledger.db");
	mWalletDB=new DatabaseCon("wallet.db");
	mHashNodeDB=new DatabaseCon("hashnode.db");
	mNetNodeDB=new DatabaseCon("netnode.db");

	if(theConfig.PEER_PORT)
	{
		mPeerDoor=new PeerDoor(mIOService);
	}//else BOOST_LOG_TRIVIAL(info) << "No Peer Port set. Not listening for connections.";

	if(theConfig.RPC_PORT)
	{
		mRPCDoor=new RPCDoor(mIOService);
	}//else BOOST_LOG_TRIVIAL(info) << "No RPC Port set. Not listening for commands.";

	mConnectionPool.connectToNetwork(mKnownNodes, mIOService); 
	mTimingService.start(mIOService);
	std::cout << "Before Run." << std::endl;
	mIOService.run(); // This blocks

	// temporary
	return;
	mWallet.load();

	//BOOST_LOG_TRIVIAL(info) << "Done.";
	std::cout << "Done." << std::endl;
}
