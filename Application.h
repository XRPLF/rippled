#ifndef __APPLICATION__
#define __APPLICATION__

#include "UniqueNodeList.h"
#include "ConnectionPool.h"
#include "KnownNodeList.h"
#include "TimingService.h"
#include "Wallet.h"
#include "database/database.h"

#include <boost/asio.hpp>

class RPCDoor;
class PeerDoor;


class Application
{
	TimingService mTimingService;
	UniqueNodeList mUNL;
	KnownNodeList mKnownNodes;
	Wallet mWallet;
	Database* mDatabase;


	ConnectionPool mConnectionPool;
	PeerDoor* mPeerDoor;
	RPCDoor* mRPCDoor;
	//Serializer* mSerializer;

	boost::asio::io_service mIOService;

	

public:
	Application();

	ConnectionPool& getConnectionPool(){ return(mConnectionPool); }
	UniqueNodeList& getUNL(){ return(mUNL); }
	Wallet& getWallet(){  return(mWallet); }
	Database* getDB(){ return(mDatabase); }

	void setDB(Database* db){ mDatabase=db; }

	//Serializer* getSerializer(){ return(mSerializer); }
	//void setSerializer(Serializer* ser){ mSerializer=ser; }
	

	void run();

	
};

extern Application* theApp;

#endif