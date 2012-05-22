#ifndef __LEDGER_CONSENSUS__
#define __LEDGER_CONSENSUS__

#include <list>

#include <boost/weak_ptr.hpp>
#include <boost/unordered/unordered_map.hpp>

#include "key.h"
#include "Transaction.h"
#include "LedgerAcquire.h"
#include "LedgerProposal.h"
#include "Peer.h"

class TransactionAcquire : public PeerSet, public boost::enable_shared_from_this<TransactionAcquire>
{ // A transaction set we are trying to acquire
public:
	typedef boost::shared_ptr<TransactionAcquire> pointer;

protected:
	SHAMap::pointer		mMap;
	bool				mHaveRoot;

	void onTimer()						{ trigger(Peer::pointer()); }
	void newPeer(Peer::pointer peer)	{ trigger(peer); }

	void done();
	void trigger(Peer::pointer);
	boost::weak_ptr<PeerSet> pmDowncast();

public:

	TransactionAcquire(const uint256& hash);

	SHAMap::pointer getMap()			{ return mMap; }

	bool takeNode(const std::list<SHAMapNode>& IDs, const std::list< std::vector<unsigned char> >& data,
		Peer::pointer);
};

class LCTransaction
{ // A transaction that may be disputed
protected:
	uint256 mTransactionID;
	int mYays, mNays;
	bool mOurPosition;
	boost::unordered_map<uint256, bool> mVotes;
	Transaction::pointer mTransaction;

public:
	typedef boost::shared_ptr<LCTransaction> pointer;

	LCTransaction(const uint256 &txID, bool ourPosition) : mTransactionID(txID), mYays(0), mNays(0),
		mOurPosition(ourPosition) { ; }

	const uint256& getTransactionID() const	{ return mTransactionID; }
	bool getOurPosition() const				{ return mOurPosition; }

	bool haveTransaction() const			{ return !!mTransaction; }
	Transaction::pointer getTransaction()	{ return mTransaction; }

	void setVote(const uint256& peer, bool votesYes);

	bool updatePosition(int timePassed);
};


class LedgerConsensus
{
protected:
	Ledger::pointer mPreviousLedger, mCurrentLedger;
	LedgerProposal::pointer mCurrentProposal;

	LedgerProposal::pointer mOurPosition;

	// Convergence tracking, trusted peers indexed by hash of public key
	boost::unordered_map<uint256, LedgerProposal::pointer, hash_SMN> mPeerPositions;

	// Transaction Sets, indexed by hash of transaction tree
	boost::unordered_map<uint256, SHAMap::pointer, hash_SMN> mComplete;
	boost::unordered_map<uint256, TransactionAcquire::pointer, hash_SMN> mAcquiring;

	// Peer sets
	boost::unordered_map<uint256, std::vector< boost::weak_ptr<Peer> >, hash_SMN> mPeerData;

	void weHave(const uint256& id, Peer::pointer avoidPeer);
	void startAcquiring(TransactionAcquire::pointer);
	SHAMap::pointer find(const uint256& hash);

	void addPosition(LedgerProposal&);
	void removePosition(LedgerProposal&);

public:
	LedgerConsensus(Ledger::pointer previousLedger, Ledger::pointer currentLedger) :
		mPreviousLedger(previousLedger), mCurrentLedger(currentLedger) { ; }

	int startup();

	Ledger::pointer peekPreviousLedger()	{ return mPreviousLedger; }
	Ledger::pointer peekCurrentLedger()		{ return mCurrentLedger; }

	SHAMap::pointer getTransactionTree(const uint256& hash, bool doAcquire);
	TransactionAcquire::pointer getAcquiring(const uint256& hash);
	void acquireComplete(const uint256& hash);

	void abort();
	int timerEntry(void);

	bool peerPosition(LedgerProposal::pointer);

	bool peerHasSet(Peer::pointer peer, const std::vector<uint256>& sets);

	bool peerGaveNodes(Peer::pointer peer, const uint256& setHash,
		const std::list<SHAMapNode>& nodeIDs, const std::list< std::vector<unsigned char> >& nodeData);
};


#endif
