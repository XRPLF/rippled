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

	bool takeNodes(const std::list<SHAMapNode>& IDs, const std::list< std::vector<unsigned char> >& data,
		Peer::pointer);
};

class LCTransaction
{ // A transaction that may be disputed
protected:
	uint256 mTransactionID;
	int mYays, mNays;
	bool mOurPosition;
	std::vector<unsigned char> transaction;
	boost::unordered_map<uint256, bool, hash_SMN> mVotes;

public:
	typedef boost::shared_ptr<LCTransaction> pointer;

	LCTransaction(const uint256 &txID, const std::vector<unsigned char>& tx, bool ourPosition) :
		mTransactionID(txID), mYays(0), mNays(0), mOurPosition(ourPosition), transaction(tx) { ; }

	const uint256& getTransactionID() const				{ return mTransactionID; }
	bool getOurPosition() const							{ return mOurPosition; }
	const std::vector<unsigned char>& peekTransaction()	{ return transaction; }

	void setVote(const uint256& peer, bool votesYes);

	bool updatePosition(int timePassed);
};

enum LCState
{
	lcsPRE_CLOSE,		// We haven't closed our ledger yet, but others might have
	lcsESTABLISH,		// Establishing consensus
	lcsCUTOFF,			// Past the cutoff for consensus
	lcsFINSHED,			// We have closed on a transaction set
	lcsACCEPTED,		// We have accepted/validated a new last closed ledger
	lcsABORTED			// Abandoned
};

class LedgerConsensus
{
protected:
	LCState mState;
	uint32 mCloseTime;
	Ledger::pointer mPreviousLedger;
	LedgerProposal::pointer mOurPosition;

	// Convergence tracking, trusted peers indexed by hash of public key
	boost::unordered_map<uint256, LedgerProposal::pointer, hash_SMN> mPeerPositions;

	// Transaction Sets, indexed by hash of transaction tree
	boost::unordered_map<uint256, SHAMap::pointer, hash_SMN> mComplete;
	boost::unordered_map<uint256, TransactionAcquire::pointer, hash_SMN> mAcquiring;

	// Peer sets
	boost::unordered_map<uint256, std::vector< boost::weak_ptr<Peer> >, hash_SMN> mPeerData;

	// Disputed transactions
	boost::unordered_map<uint256, LCTransaction::pointer, hash_SMN> mDisputes;

	void weHave(const uint256& id, Peer::pointer avoidPeer);
	void startAcquiring(TransactionAcquire::pointer);
	SHAMap::pointer find(const uint256& hash);

	void addDisputedTransaction(const uint256&, const std::vector<unsigned char>& transaction);
	void adjustCount(SHAMap::pointer map, const std::vector<uint256>& peers);

	void addPosition(LedgerProposal&, bool ours);
	void removePosition(LedgerProposal&, bool ours);
	void sendHaveTxSet(const std::vector<uint256>& txSetHashes);
	int getThreshold();

public:
	LedgerConsensus(Ledger::pointer previousLedger, uint32 closeTime);
	void closeTime(Ledger::pointer& currentLedger);

	int startup();

	Ledger::pointer peekPreviousLedger()	{ return mPreviousLedger; }

	SHAMap::pointer getTransactionTree(const uint256& hash, bool doAcquire);
	TransactionAcquire::pointer getAcquiring(const uint256& hash);
	void mapComplete(const uint256& hash, SHAMap::pointer map);

	void abort();
	int timerEntry(void);

	bool peerPosition(LedgerProposal::pointer);

	bool peerHasSet(Peer::pointer peer, const std::vector<uint256>& sets);

	bool peerGaveNodes(Peer::pointer peer, const uint256& setHash,
		const std::list<SHAMapNode>& nodeIDs, const std::list< std::vector<unsigned char> >& nodeData);
};


#endif
