
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

Application::Application()
{
	theConfig.load();
	//mUNL.load();
	mPeerDoor=NULL;
	mRPCDoor=NULL;
	mDatabase=NULL;


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
	std::string filename=strprintf("%sdata.db",theConfig.DATA_DIR.c_str());
	theApp->setDB(new SqliteDatabase(filename.c_str()));
	mDatabase->connect();

	return; // TEMPORARY

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

	mWallet.load();

	//BOOST_LOG_TRIVIAL(info) << "Done.";
	std::cout << "Done." << std::endl;
}
