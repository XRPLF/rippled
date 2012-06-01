#ifndef __APPLICATION__
#define __APPLICATION__

#include "LedgerMaster.h"
#include "UniqueNodeList.h"
#include "ConnectionPool.h"
#include "ScopedLock.h"
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
	Database*				mDatabase;
	boost::recursive_mutex	mLock;

public:
	DatabaseCon(const std::string& name, const char *initString[], int countInit);
	~DatabaseCon();
	Database* getDB() { return mDatabase; }
	ScopedLock getDBLock() { return ScopedLock(mLock); }
};

class Application
{
	boost::asio::io_service	mIOService;

	Wallet					mWallet;
	UniqueNodeList			mUNL;
	LedgerMaster			mMasterLedger;
	LedgerAcquireMaster		mMasterLedgerAcquire;
	TransactionMaster		mMasterTransaction;
	NetworkOPs				mNetOps;

	DatabaseCon* mTxnDB, *mLedgerDB, *mWalletDB, *mHashNodeDB, *mNetNodeDB;

	ConnectionPool			mConnectionPool;
	PeerDoor*				mPeerDoor;
	RPCDoor*				mRPCDoor;

	uint256					mNonce256;

	std::map<std::string, Peer::pointer> mPeerMap;
	boost::recursive_mutex	mPeerMapLock;

public:
	Application();
	~Application();

	ConnectionPool& getConnectionPool() { return mConnectionPool; }

	UniqueNodeList& getUNL() { return mUNL; }

	Wallet& getWallet() { return mWallet ; }
	NetworkOPs& getOPs() { return mNetOps; }

	boost::asio::io_service& getIOService() { return mIOService; }

	LedgerMaster& getMasterLedger() { return mMasterLedger; }
	LedgerAcquireMaster& getMasterLedgerAcquire() { return mMasterLedgerAcquire; }
	TransactionMaster& getMasterTransaction() { return mMasterTransaction; }

	DatabaseCon* getTxnDB() { return mTxnDB; }
	DatabaseCon* getLedgerDB() { return mLedgerDB; }
	DatabaseCon* getWalletDB() { return mWalletDB; }
	DatabaseCon* getHashNodeDB() { return mHashNodeDB; }
	DatabaseCon* getNetNodeDB() { return mNetNodeDB; }

	uint256 getNonce256() { return mNonce256; }

	void run();
	void stop();
};

extern Application* theApp;

#endif
// vim:ts=4
