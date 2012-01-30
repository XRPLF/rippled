#ifndef __LEDGERACQUIRE__
#define __LEDGERACQUIRE__

#include <vector>
#include <map>

#include "boost/enable_shared_from_this.hpp"
#include "boost/function.hpp"

#include "Ledger.h"
#include "Peer.h"

class LedgerAcquire : public boost::enable_shared_from_this<LedgerAcquire>
{ // A ledger we are trying to acquire
public:
	typedef boost::shared_ptr<LedgerAcquire> pointer;

protected:
	boost::recursive_mutex mLock;
	Ledger::pointer mLedger;
	uint256 mHash;
	bool mComplete, mFailed, mHaveBase, mHaveState, mHaveTransactions;
	std::vector< boost::function<void (LedgerAcquire::pointer)> > mOnComplete;

	std::list<boost::weak_ptr<Peer> > mPeers; // peers known to have this ledger

	void done();
	void trigger(bool timer);

	static void timerEntry(boost::weak_ptr<LedgerAcquire>);

public:
	LedgerAcquire(const uint256& hash);

	const uint256& getHash() const 		{ return mHash; }
	bool isComplete() const 			{ return mComplete; }
	bool isFailed() const 				{ return mFailed; }
	bool isBase() const 				{ return mHaveBase; }
	bool isAcctStComplete() const 		{ return mHaveState; }
	bool isTransComplete() const 		{ return mHaveTransactions; }
	Ledger::pointer getLedger()			{ return mLedger; }		

	void addOnComplete(boost::function<void (LedgerAcquire::pointer)>);

	void peerHas(Peer::pointer);
	void badPeer(Peer::pointer);
	bool takeBase(std::vector<unsigned char> data);
	bool takeTxNode(std::list<uint256> hashes, std::list<std::vector<unsigned char> > data);
	bool takeAsNode(std::list<uint160> hashes, std::list<std::vector<unsigned char> > data);
	bool takeTx(std::list<uint256> hashes, std::list<std::vector<unsigned char> > data);
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
};

#endif
