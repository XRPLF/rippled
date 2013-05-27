#ifndef __LEDGERACQUIRE__
#define __LEDGERACQUIRE__

#include <vector>
#include <map>
#include <set>
#include <list>

#include <boost/enable_shared_from_this.hpp>
#include <boost/function.hpp>
#include <boost/asio.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/unordered_map.hpp>
#include <boost/weak_ptr.hpp>

#include "Ledger.h"
#include "Peer.h"
#include "InstanceCounter.h"
#include "ripple.pb.h"

// How long before we try again to acquire the same ledger
#ifndef LEDGER_REACQUIRE_INTERVAL
#define LEDGER_REACQUIRE_INTERVAL 600
#endif

DEFINE_INSTANCE(LedgerAcquire);

class PeerSet
{
protected:
	uint256 mHash;
	int mTimerInterval, mTimeouts;
	bool mComplete, mFailed, mProgress, mAggressive;
	int mLastAction;

	boost::recursive_mutex					mLock;
	boost::asio::deadline_timer				mTimer;
	boost::unordered_map<uint64, int>		mPeers;

	PeerSet(const uint256& hash, int interval);
	virtual ~PeerSet() { ; }

	void sendRequest(const ripple::TMGetLedger& message);
	void sendRequest(const ripple::TMGetLedger& message, Peer::ref peer);

public:
	const uint256& getHash() const		{ return mHash; }
	bool isComplete() const				{ return mComplete; }
	bool isFailed() const				{ return mFailed; }
	int getTimeouts() const				{ return mTimeouts; }

	bool isActive();
	void progress()						{ mProgress = true; mAggressive = false; }
	bool isProgress()					{ return mProgress; }
	void touch()						{ mLastAction = UptimeTimer::getInstance().getElapsedSeconds(); }
	int getLastAction()					{ return mLastAction; }

	void peerHas(Peer::ref);
	void badPeer(Peer::ref);
	void setTimer();

	int takePeerSetFrom(const PeerSet& s);
	int getPeerCount() const;
	virtual bool isDone() const			{ return mComplete || mFailed; }

protected:
	virtual void newPeer(Peer::ref) = 0;
	virtual void onTimer(bool progress) = 0;
	virtual boost::weak_ptr<PeerSet> pmDowncast() = 0;

	void setComplete()					{ mComplete = true; }
	void setFailed()					{ mFailed = true; }
	void invokeOnTimer();

private:
	static void TimerEntry(boost::weak_ptr<PeerSet>, const boost::system::error_code& result);
	static void TimerJobEntry(Job&, boost::shared_ptr<PeerSet>);
};

class LedgerAcquire :
	private IS_INSTANCE(LedgerAcquire), public PeerSet, public boost::enable_shared_from_this<LedgerAcquire>
{ // A ledger we are trying to acquire
public:
	typedef boost::shared_ptr<LedgerAcquire> pointer;

protected:
	Ledger::pointer			mLedger;
	bool					mHaveBase, mHaveState, mHaveTransactions, mAborted, mSignaled, mAccept, mByHash;
	int						mWaitCount;
	uint32					mSeq;

	std::set<SHAMapNode>	mRecentTXNodes;
	std::set<SHAMapNode>	mRecentASNodes;

	std::vector<uint64>		mRecentPeers;

	std::vector< FUNCTION_TYPE<void (LedgerAcquire::pointer)> >	mOnComplete;

	void done();
	void onTimer(bool progress);

	void newPeer(Peer::ref peer) { trigger(peer); }

	boost::weak_ptr<PeerSet> pmDowncast();

public:
	LedgerAcquire(const uint256& hash, uint32 seq);
	virtual ~LedgerAcquire()			{ ; }

	bool isBase() const					{ return mHaveBase; }
	bool isAcctStComplete() const		{ return mHaveState; }
	bool isTransComplete() const		{ return mHaveTransactions; }
	bool isDone() const					{ return mAborted || isComplete() || isFailed(); } 
	Ledger::ref getLedger()				{ return mLedger; }
	void abort()						{ mAborted = true; }
	bool setAccept()					{ if (mAccept) return false; mAccept = true; return true; }

	bool addOnComplete(FUNCTION_TYPE<void (LedgerAcquire::pointer)>);

	bool takeBase(const std::string& data);
	bool takeTxNode(const std::list<SHAMapNode>& IDs, const std::list<std::vector<unsigned char> >& data,
		SMAddNode&);
	bool takeTxRootNode(const std::vector<unsigned char>& data, SMAddNode&);
	bool takeAsNode(const std::list<SHAMapNode>& IDs, const std::list<std::vector<unsigned char> >& data,
		SMAddNode&);
	bool takeAsRootNode(const std::vector<unsigned char>& data, SMAddNode&);
	void trigger(Peer::ref);
	bool tryLocal();
	void addPeers();
	void awaitData();
	void noAwaitData();
	void checkLocal();

	typedef std::pair<ripple::TMGetObjectByHash::ObjectType, uint256> neededHash_t;
	std::vector<neededHash_t> getNeededHashes();

	static void filterNodes(std::vector<SHAMapNode>& nodeIDs, std::vector<uint256>& nodeHashes,
		std::set<SHAMapNode>& recentNodes, int max, bool aggressive);

	Json::Value getJson(int);
};

class LedgerAcquireMaster
{
protected:
	boost::mutex mLock;
	std::map<uint256, LedgerAcquire::pointer> mLedgers;
	KeyCache<uint256, UptimeTimerAdapter> mRecentFailures;

public:
	LedgerAcquireMaster() : mRecentFailures("LedgerAcquireRecentFailures", 0, LEDGER_REACQUIRE_INTERVAL) { ; }

	LedgerAcquire::pointer findCreate(const uint256& hash, uint32 seq);
	LedgerAcquire::pointer find(const uint256& hash);
	bool hasLedger(const uint256& ledgerHash);
	void dropLedger(const uint256& ledgerHash);

	bool awaitLedgerData(const uint256& ledgerHash);
	void gotLedgerData(Job&, uint256 hash, boost::shared_ptr<ripple::TMLedgerData> packet, boost::weak_ptr<Peer> peer);

	int getFetchCount(int& timeoutCount);
	void logFailure(const uint256& h)	{ mRecentFailures.add(h); }
	bool isFailure(const uint256& h)	{ return mRecentFailures.isPresent(h, false); }

	void gotFetchPack(Job&);
	void sweep();
};

#endif

// vim:ts=4
