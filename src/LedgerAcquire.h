#ifndef __LEDGERACQUIRE__
#define __LEDGERACQUIRE__

#include <vector>
#include <map>
#include <list>

#include <boost/enable_shared_from_this.hpp>
#include <boost/function.hpp>
#include <boost/asio.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/weak_ptr.hpp>

#include "Ledger.h"
#include "Peer.h"
#include "TaggedCache.h"
#include "../obj/src/newcoin.pb.h"

class PeerSet
{
protected:
	uint256 mHash;
	int mTimerInterval, mTimeouts;
	bool mComplete, mFailed, mProgress;

	boost::recursive_mutex					mLock;
	boost::asio::deadline_timer				mTimer;
	std::vector< boost::weak_ptr<Peer> >	mPeers;

	PeerSet(const uint256& hash, int interval);
	virtual ~PeerSet() { ; }

	void sendRequest(const newcoin::TMGetLedger& message);
	void sendRequest(const newcoin::TMGetLedger& message, Peer::pointer peer);

public:
	const uint256& getHash() const		{ return mHash; }
	bool isComplete() const				{ return mComplete; }
	bool isFailed() const				{ return mFailed; }
	int getTimeouts() const				{ return mTimeouts; }

	void progress()						{ mProgress = true; }

	void peerHas(const Peer::pointer&);
	void badPeer(const Peer::pointer&);
	void resetTimer();

protected:
	virtual void newPeer(const Peer::pointer&) = 0;
	virtual void onTimer(void) = 0;
	virtual boost::weak_ptr<PeerSet> pmDowncast() = 0;

	void setComplete()					{ mComplete = true; }
	void setFailed()					{ mFailed = true; }
	void invokeOnTimer();

private:
	static void TimerEntry(boost::weak_ptr<PeerSet>, const boost::system::error_code& result);
};

class LedgerAcquire : public PeerSet, public boost::enable_shared_from_this<LedgerAcquire>
{ // A ledger we are trying to acquire
public:
	typedef boost::shared_ptr<LedgerAcquire> pointer;

protected:
	Ledger::pointer mLedger;
	bool mHaveBase, mHaveState, mHaveTransactions, mAborted, mSignaled;

	std::vector< boost::function<void (LedgerAcquire::pointer)> > mOnComplete;

	void done();
	void onTimer();

	void newPeer(const Peer::pointer& peer) { trigger(peer, false); }

	boost::weak_ptr<PeerSet> pmDowncast();

public:
	LedgerAcquire(const uint256& hash);

	bool isBase() const					{ return mHaveBase; }
	bool isAcctStComplete() const		{ return mHaveState; }
	bool isTransComplete() const		{ return mHaveTransactions; }
	Ledger::pointer getLedger()			{ return mLedger; }
	void abort()						{ mAborted = true; }

	void addOnComplete(boost::function<void (LedgerAcquire::pointer)>);

	bool takeBase(const std::string& data);
	bool takeTxNode(const std::list<SHAMapNode>& IDs, const std::list<std::vector<unsigned char> >& data);
	bool takeTxRootNode(const std::vector<unsigned char>& data);
	bool takeAsNode(const std::list<SHAMapNode>& IDs, const std::list<std::vector<unsigned char> >& data);
	bool takeAsRootNode(const std::vector<unsigned char>& data);
	void trigger(const Peer::pointer&, bool timer);
};

class LedgerAcquireMaster
{
protected:
	boost::mutex mLock;
	std::map<uint256, LedgerAcquire::pointer> mLedgers;

public:
	LedgerAcquireMaster() { ; }

	LedgerAcquire::pointer findCreate(const uint256& hash);
	LedgerAcquire::pointer find(const uint256& hash);
	bool hasLedger(const uint256& ledgerHash);
	void dropLedger(const uint256& ledgerHash);
	bool gotLedgerData(newcoin::TMLedgerData& packet, const Peer::pointer&);
};

#endif

// vim:ts=4
