#ifndef __APPLICATION__
#define __APPLICATION__

#include "leveldb/db.h"

#include <boost/asio.hpp>

#include "../database/database.h"

#include "LedgerMaster.h"
#include "UniqueNodeList.h"
#include "ConnectionPool.h"
#include "FeatureTable.h"
#include "LedgerAcquire.h"
#include "TransactionMaster.h"
#include "Wallet.h"
#include "Peer.h"
#include "NetworkOPs.h"
#include "WSDoor.h"
#include "ValidationCollection.h"
#include "Suppression.h"
#include "SNTPClient.h"
#include "JobQueue.h"
#include "RPCHandler.h"
#include "ProofOfWork.h"
#include "LoadManager.h"
#include "TransactionQueue.h"
#include "OrderBookDB.h"

// VFALCO: TODO, Fix forward declares required for header dependency loops
class IFeatureTable;
class IFeeVote;

class RPCDoor;
class PeerDoor;
typedef TaggedCache< uint256, std::vector<unsigned char>, UptimeTimerAdapter> NodeCache;
typedef TaggedCache< uint256, SLE, UptimeTimerAdapter> SLECache;

class DatabaseCon
{
protected:
	Database*				mDatabase;
	boost::recursive_mutex	mLock;
	static int				sCount;

public:
	DatabaseCon(const std::string& name, const char *initString[], int countInit);
	~DatabaseCon();
	Database* getDB()						{ return mDatabase; }
	boost::recursive_mutex& getDBLock()		{ return mLock; }
	static int getCount()					{ return sCount; }
};

class Application
{
	boost::asio::io_service			mIOService, mAuxService;
	boost::asio::io_service::work	mIOWork, mAuxWork;

	boost::recursive_mutex	mMasterLock;

	Wallet					mWallet;
	UniqueNodeList			mUNL;
	LedgerMaster			mLedgerMaster;
	LedgerAcquireMaster		mMasterLedgerAcquire;
	TransactionMaster		mMasterTransaction;
	NetworkOPs				mNetOps;
	NodeCache				mTempNodeCache;
	ValidationCollection	mValidations;
	SuppressionTable		mSuppressions;
	HashedObjectStore		mHashedObjectStore;
	SLECache				mSLECache;
	SNTPClient				mSNTPClient;
	JobQueue				mJobQueue;
	ProofOfWorkGenerator	mPOWGen;
	LoadManager				mLoadMgr;
	LoadFeeTrack			mFeeTrack;
	TXQueue					mTxnQueue;
	OrderBookDB				mOrderBookDB;
	IFeeVote*				mFeeVote;
	FeatureTable			mFeatureTable;

	DatabaseCon				*mRpcDB, *mTxnDB, *mLedgerDB, *mWalletDB, *mNetNodeDB, *mPathFindDB, *mHashNodeDB;

	leveldb::DB				*mHashNodeLDB;
	leveldb::DB				*mEphemeralLDB;

	ConnectionPool			mConnectionPool;
	PeerDoor*				mPeerDoor;
	RPCDoor*				mRPCDoor;
	WSDoor*					mWSPublicDoor;
	WSDoor*					mWSPrivateDoor;

	uint256					mNonce256;
	std::size_t				mNonceST;

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

	UniqueNodeList& getUNL()						{ return mUNL; }

	Wallet& getWallet()								{ return mWallet ; }
	NetworkOPs& getOPs()							{ return mNetOps; }

	boost::asio::io_service& getIOService()			{ return mIOService; }
	boost::asio::io_service& getAuxService()		{ return mAuxService; }

	LedgerMaster& getLedgerMaster()					{ return mLedgerMaster; }
	LedgerAcquireMaster& getMasterLedgerAcquire()	{ return mMasterLedgerAcquire; }
	TransactionMaster& getMasterTransaction()		{ return mMasterTransaction; }
	NodeCache& getTempNodeCache()					{ return mTempNodeCache; }
	HashedObjectStore& getHashedObjectStore()		{ return mHashedObjectStore; }
	ValidationCollection& getValidations()			{ return mValidations; }
	JobQueue& getJobQueue()							{ return mJobQueue; }
	SuppressionTable& getSuppression()				{ return mSuppressions; }
	boost::recursive_mutex& getMasterLock()			{ return mMasterLock; }
	ProofOfWorkGenerator& getPowGen()				{ return mPOWGen; }
	LoadManager& getLoadManager()					{ return mLoadMgr; }
	LoadFeeTrack& getFeeTrack()						{ return mFeeTrack; }
	TXQueue& getTxnQueue()							{ return mTxnQueue; }
	PeerDoor& getPeerDoor()							{ return *mPeerDoor; }
	OrderBookDB& getOrderBookDB()					{ return mOrderBookDB; }
	SLECache& getSLECache()							{ return mSLECache; }
	IFeeVote& getFeeVote()							{ return *mFeeVote; }
	FeatureTable& getFeatureTable()					{ return mFeatureTable; }


	bool isNew(const uint256& s)					{ return mSuppressions.addSuppression(s); }
	bool isNew(const uint256& s, uint64 p)			{ return mSuppressions.addSuppressionPeer(s, p); }
	bool isNew(const uint256& s, uint64 p, int& f)	{ return mSuppressions.addSuppressionPeer(s, p, f); }
	bool isNewFlag(const uint256& s, int f)			{ return mSuppressions.setFlag(s, f); }
	bool running()									{ return mTxnDB != NULL; }
	bool getSystemTimeOffset(int& offset)			{ return mSNTPClient.getOffset(offset); }

	DatabaseCon* getRpcDB()			{ return mRpcDB; }
	DatabaseCon* getTxnDB()			{ return mTxnDB; }
	DatabaseCon* getLedgerDB()		{ return mLedgerDB; }
	DatabaseCon* getWalletDB()		{ return mWalletDB; }
	DatabaseCon* getNetNodeDB()		{ return mNetNodeDB; }
	DatabaseCon* getPathFindDB()	{ return mPathFindDB; }
	DatabaseCon* getHashNodeDB()	{ return mHashNodeDB; }

	leveldb::DB* getHashNodeLDB()	{ return mHashNodeLDB; }
	leveldb::DB* getEphemeralLDB()	{ return mEphemeralLDB; }

	uint256 getNonce256()			{ return mNonce256; }
	std::size_t getNonceST()		{ return mNonceST; }

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
