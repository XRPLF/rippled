#ifndef __APPLICATION__
#define __APPLICATION__

#ifdef USE_LEVELDB
#include "leveldb/db.h"
#endif

#include <boost/asio.hpp>

#include "../database/database.h"

#include "LedgerMaster.h"
#include "ConnectionPool.h"
#include "LedgerAcquire.h"
#include "TransactionMaster.h"
#include "Wallet.h"
#include "NetworkOPs.h"
#include "WSDoor.h"
#include "SNTPClient.h"
#include "RPCHandler.h"
#include "LoadManager.h"
#include "TransactionQueue.h"
#include "OrderBookDB.h"

#include "ripple_DatabaseCon.h"

// VFALCO: TODO, Fix forward declares required for header dependency loops
class IFeatures;
class IFeeVote;
class IHashRouter;
class ILoadFeeTrack;
class IValidations;
class IUniqueNodeList;
class IProofOfWorkFactory;

class RPCDoor;
class PeerDoor;
typedef TaggedCache< uint256, std::vector<unsigned char>, UptimeTimerAdapter> NodeCache;
typedef TaggedCache< uint256, SLE, UptimeTimerAdapter> SLECache;

class Application
{
	boost::asio::io_service			mIOService, mAuxService;
	boost::asio::io_service::work	mIOWork, mAuxWork;

	boost::recursive_mutex	mMasterLock;

	Wallet					mWallet;
	LedgerMaster			mLedgerMaster;
	LedgerAcquireMaster		mMasterLedgerAcquire;
	TransactionMaster		mMasterTransaction;
	NetworkOPs				mNetOps;
	NodeCache				mTempNodeCache;
	HashedObjectStore		mHashedObjectStore;
	SLECache				mSLECache;
	SNTPClient				mSNTPClient;
	JobQueue				mJobQueue;
	LoadManager				mLoadMgr;
	TXQueue					mTxnQueue;
	OrderBookDB				mOrderBookDB;
	
    // VFALCO: Clean stuff
    beast::ScopedPointer <IFeatures> mFeatures;
	beast::ScopedPointer <IFeeVote> mFeeVote;
    beast::ScopedPointer <ILoadFeeTrack> mFeeTrack;
    beast::ScopedPointer <IHashRouter> mHashRouter;
	beast::ScopedPointer <IValidations> mValidations;
	beast::ScopedPointer <IUniqueNodeList> mUNL;
	beast::ScopedPointer <IProofOfWorkFactory> mProofOfWorkFactory;
    // VFALCO: End Clean stuff

	DatabaseCon				*mRpcDB, *mTxnDB, *mLedgerDB, *mWalletDB, *mNetNodeDB, *mPathFindDB, *mHashNodeDB;

#ifdef USE_LEVELDB
	leveldb::DB				*mHashNodeLDB;
#endif

	ConnectionPool			mConnectionPool;
	PeerDoor*				mPeerDoor;
	RPCDoor*				mRPCDoor;
	WSDoor*					mWSPublicDoor;
	WSDoor*					mWSPrivateDoor;

	boost::asio::deadline_timer	mSweepTimer;

	std::map<std::string, Peer::pointer> mPeerMap;
	boost::recursive_mutex	mPeerMapLock;

	volatile bool			mShutdown;

	void updateTables(bool);
	void startNewLedger();
	bool loadOldLedger(const std::string&);

public:
	Application();
	~Application();

	ConnectionPool& getConnectionPool()				{ return mConnectionPool; }

	IUniqueNodeList& getUNL()						{ return *mUNL; }

	Wallet& getWallet()								{ return mWallet ; }
	NetworkOPs& getOPs()							{ return mNetOps; }

	boost::asio::io_service& getIOService()			{ return mIOService; }
	boost::asio::io_service& getAuxService()		{ return mAuxService; }

	LedgerMaster& getLedgerMaster()					{ return mLedgerMaster; }
	LedgerAcquireMaster& getMasterLedgerAcquire()	{ return mMasterLedgerAcquire; }
	TransactionMaster& getMasterTransaction()		{ return mMasterTransaction; }
	NodeCache& getTempNodeCache()					{ return mTempNodeCache; }
	HashedObjectStore& getHashedObjectStore()		{ return mHashedObjectStore; }
	JobQueue& getJobQueue()							{ return mJobQueue; }
	boost::recursive_mutex& getMasterLock()			{ return mMasterLock; }
	LoadManager& getLoadManager()					{ return mLoadMgr; }
	TXQueue& getTxnQueue()							{ return mTxnQueue; }
	PeerDoor& getPeerDoor()							{ return *mPeerDoor; }
	OrderBookDB& getOrderBookDB()					{ return mOrderBookDB; }
	SLECache& getSLECache()							{ return mSLECache; }

    IFeatures& getFeatureTable()				    { return *mFeatures; }
	ILoadFeeTrack& getFeeTrack()				    { return *mFeeTrack; }
	IFeeVote& getFeeVote()							{ return *mFeeVote; }
	IHashRouter& getHashRouter()				    { return *mHashRouter; }
	IValidations& getValidations()			        { return *mValidations; }
	IProofOfWorkFactory& getProofOfWorkFactory()    { return *mProofOfWorkFactory; }

    // VFALCO: TODO, Move these to the .cpp
    bool running()									{ return mTxnDB != NULL; } // VFALCO: TODO, replace with nullptr when beast is available
	bool getSystemTimeOffset(int& offset)			{ return mSNTPClient.getOffset(offset); }

	DatabaseCon* getRpcDB()			{ return mRpcDB; }
	DatabaseCon* getTxnDB()			{ return mTxnDB; }
	DatabaseCon* getLedgerDB()		{ return mLedgerDB; }
	DatabaseCon* getWalletDB()		{ return mWalletDB; }
	DatabaseCon* getNetNodeDB()		{ return mNetNodeDB; }
	DatabaseCon* getPathFindDB()	{ return mPathFindDB; }
	DatabaseCon* getHashNodeDB()	{ return mHashNodeDB; }

#ifdef USE_LEVELDB
	leveldb::DB* getHashNodeLDB()	{ return mHashNodeLDB; }
#endif

	bool isShutdown()				{ return mShutdown; }
	void setup();
	void run();
	void stop();
	void sweep();

#ifdef DEBUG
	void mustHaveMasterLock()		{ bool tl = mMasterLock.try_lock(); assert(tl); mMasterLock.unlock(); }
#else
	void mustHaveMasterLock()		{ ; }
#endif

};

extern Application* theApp;

#endif
// vim:ts=4
