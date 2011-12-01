#include "Application.h"
#include "Config.h"
#include "PeerDoor.h"
#include "RPCDoor.h"
#include "BitcoinUtil.h"
#include "database/SqliteDatabase.h"
//#include <boost/log/trivial.hpp>
#include <iostream>
using namespace std;

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
	mKnownNodes.load();
	//mUNL.load();
	mWallet.load();
	mPeerDoor=NULL;
	mRPCDoor=NULL;
}


void Application::run()
{
	string filename=strprintf("%sdata.db",theConfig.DATA_DIR.c_str());
	theApp->setDB(new SqliteDatabase(filename.c_str()));
	mDatabase->connect();

	if(theConfig.PEER_PORT)
	{
		mPeerDoor=new PeerDoor(mIOService);
	}//else BOOST_LOG_TRIVIAL(info) << "No Peer Port set. Not listening for connections.";

	if(theConfig.RPC_PORT)
	{
		mRPCDoor=new RPCDoor(mIOService);
	}//else BOOST_LOG_TRIVIAL(info) << "No RPC Port set. Not listening for commands.";

	mConnectionPool.connectToNetwork(mKnownNodes,mIOService); 
	mTimingService.start(mIOService);
	cout << "Before Run." << endl;
	mIOService.run();// This blocks

	//BOOST_LOG_TRIVIAL(info) << "Done.";
	cout << "Done." << endl;
}




