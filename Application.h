#include "UniqueNodeList.h"
#include "ConnectionPool.h"
#include "KnownNodeList.h"
#include "LedgerMaster.h"
#include "TimingService.h"
#include "ValidationCollection.h"
#include "Wallet.h"
#include <boost/asio.hpp>

class RPCDoor;
class PeerDoor;


class Application
{
	TimingService mTimingService;
	UniqueNodeList mUNL;
	KnownNodeList mKnownNodes;
	Wallet mWallet;
	ValidationCollection mValidations;

	LedgerMaster mLedgerMaster;

	ConnectionPool mConnectionPool;
	PeerDoor* mPeerDoor;
	RPCDoor* mRPCDoor;

	boost::asio::io_service mIOService;

	

public:
	Application();

	ConnectionPool& getConnectionPool(){ return(mConnectionPool); }
	LedgerMaster& getLedgerMaster(){ return(mLedgerMaster); }
	UniqueNodeList& getUNL(){ return(mUNL); }
	ValidationCollection& getValidationCollection(){  return(mValidations); }
	Wallet& getWallet(){  return(mWallet); }
	

	void run();

	
};

extern Application* theApp;