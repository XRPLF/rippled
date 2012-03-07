#ifndef __APPLICATION__
#define __APPLICATION__

#include "UniqueNodeList.h"
#include "ConnectionPool.h"
#include "KnownNodeList.h"
#include "TimingService.h"
#include "PubKeyCache.h"
#include "ScopedLock.h"
#include "LedgerMaster.h"
#include "LedgerAcquire.h"
#include "TransactionMaster.h"
#include "Wallet.h"
#include "Peer.h"
#include "NetworkOPs.h"
#include "../database/database.h"

#include <boost/asio.hpp>

class RPCDoor;
class PeerDoor;

class DatabaseCon
{
protected:
	Database *mDatabase;
	boost::recursive_mutex mLock;
	
public:
	DatabaseCon(const std::string& name, const char *initString[], int countInit);
	~DatabaseCon();
	Database* getDB() { return mDatabase; }
	ScopedLock getDBLock() { return ScopedLock(mLock); }
};

class Application
{
	NetworkOPs mNetOps;
	Wallet mWallet;

	TimingService mTimingService;
	UniqueNodeList mUNL;
	KnownNodeList mKnownNodes;
	PubKeyCache mPKCache;
	LedgerMaster mMasterLedger;
	LedgerAcquireMaster mMasterLedgerAcquire;
	TransactionMaster mMasterTransaction;

	DatabaseCon *mTxnDB, *mLedgerDB, *mWalletDB, *mHashNodeDB, *mNetNodeDB;

	ConnectionPool mConnectionPool;
	PeerDoor* mPeerDoor;
	RPCDoor* mRPCDoor;

	std::map<std::string, Peer::pointer> mPeerMap;
	boost::recursive_mutex mPeerMapLock;

	boost::asio::io_service mIOService;
	

public:
	Application();
	~Application();

	ConnectionPool& getConnectionPool() { return mConnectionPool; }

	UniqueNodeList& getUNL() { return mUNL; }

	Wallet& getWallet() { return mWallet ; }
	NetworkOPs& getOPs() { return mNetOps; }

	PubKeyCache& getPubKeyCache() { return mPKCache; }

	boost::asio::io_service& getIOService() { return mIOService; }

	LedgerMaster& getMasterLedger() { return mMasterLedger; }
	LedgerAcquireMaster& getMasterLedgerAcquire() { return mMasterLedgerAcquire; }
	TransactionMaster& getMasterTransaction() { return mMasterTransaction; }
	
	DatabaseCon* getTxnDB() { return mTxnDB; }
	DatabaseCon* getLedgerDB() { return mLedgerDB; }
	DatabaseCon* getWalletDB() { return mWalletDB; }
	DatabaseCon* getHashNodeDB() { return mHashNodeDB; }
	DatabaseCon* getNetNodeDB() { return mNetNodeDB; }

	//Serializer* getSerializer(){ return(mSerializer); }
	//void setSerializer(Serializer* ser){ mSerializer=ser; }
	

	void run();

	
};

extern Application* theApp;

#endif
