#ifndef __LEDGER_CONSENSUS__
#define __LEDGER_CONSENSUS__

#include <list>
#include <map>

#include <boost/weak_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/unordered/unordered_map.hpp>

#include "../json/value.h"

#include "key.h"
#include "Transaction.h"
#include "LedgerAcquire.h"
#include "LedgerProposal.h"
#include "Peer.h"
#include "CanonicalTXSet.h"
#include "TransactionEngine.h"

class TransactionAcquire : public PeerSet, public boost::enable_shared_from_this<TransactionAcquire>
{ // A transaction set we are trying to acquire
public:
	typedef boost::shared_ptr<TransactionAcquire> pointer;

protected:
	SHAMap::pointer		mMap;
	bool				mHaveRoot;

	void onTimer()						{ trigger(Peer::pointer(), true); }
	void newPeer(Peer::pointer peer)	{ trigger(peer, false); }

	void done();
	void trigger(Peer::pointer, bool timer);
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
	Serializer transaction;
	boost::unordered_map<uint160, bool> mVotes;

public:
	typedef boost::shared_ptr<LCTransaction> pointer;

	LCTransaction(const uint256 &txID, const std::vector<unsigned char>& tx, bool ourPosition) :
		mTransactionID(txID), mYays(0), mNays(0), mOurPosition(ourPosition), transaction(tx) { ; }

	const uint256& getTransactionID() const				{ return mTransactionID; }
	bool getOurPosition() const							{ return mOurPosition; }
	Serializer& peekTransaction()						{ return transaction; }

	void setVote(const uint160& peer, bool votesYes);

	bool updatePosition(int percentTime, bool proposing);
};

enum LCState
{
	lcsPRE_CLOSE,		// We haven't closed our ledger yet, but others might have
	lcsESTABLISH,		// Establishing consensus
	lcsFINISHED,		// We have closed on a transaction set
	lcsACCEPTED,		// We have accepted/validated a new last closed ledger
};

class LedgerConsensus : public boost::enable_shared_from_this<LedgerConsensus>
{
protected:
	LCState mState;
	uint32 mCloseTime;						// The wall time this ledger closed
	uint256 mPrevLedgerHash, mNewLedgerHash;
	Ledger::pointer mPreviousLedger;
	LedgerAcquire::pointer mAcquiringLedger;
	LedgerProposal::pointer mOurPosition;
	NewcoinAddress mValSeed;
	bool mProposing, mValidating, mHaveCorrectLCL;

	int mCurrentMSeconds, mClosePercent, mCloseResolution;
	bool mHaveCloseTimeConsensus;

	boost::posix_time::ptime		mConsensusStartTime;
	int								mPreviousProposers;
	int								mPreviousMSeconds;

	// Convergence tracking, trusted peers indexed by hash of public key
	boost::unordered_map<uint160, LedgerProposal::pointer> mPeerPositions;

	// Transaction Sets, indexed by hash of transaction tree
	boost::unordered_map<uint256, SHAMap::pointer> mComplete;
	boost::unordered_map<uint256, TransactionAcquire::pointer> mAcquiring;

	// Peer sets
	boost::unordered_map<uint256, std::vector< boost::weak_ptr<Peer> > > mPeerData;

	// Disputed transactions
	boost::unordered_map<uint256, LCTransaction::pointer> mDisputes;

	// Close time estimates
	std::map<uint32, int> mCloseTimes;

	// final accept logic
	static void Saccept(boost::shared_ptr<LedgerConsensus> This, SHAMap::pointer txSet);
	void accept(SHAMap::pointer txSet);

	void weHave(const uint256& id, Peer::pointer avoidPeer);
	void startAcquiring(TransactionAcquire::pointer);
	SHAMap::pointer find(const uint256& hash);

	void createDisputes(SHAMap::pointer, SHAMap::pointer);
	void addDisputedTransaction(const uint256&, const std::vector<unsigned char>& transaction);
	void adjustCount(SHAMap::pointer map, const std::vector<uint160>& peers);
	void propose(const std::vector<uint256>& addedTx, const std::vector<uint256>& removedTx);

	void addPosition(LedgerProposal&, bool ours);
	void removePosition(LedgerProposal&, bool ours);
	void sendHaveTxSet(const uint256& set, bool direct);
	void applyTransactions(SHAMap::pointer transactionSet, Ledger::pointer targetLedger, Ledger::pointer checkLedger,
		CanonicalTXSet& failedTransactions, bool final);
	void applyTransaction(TransactionEngine& engine, SerializedTransaction::pointer txn, Ledger::pointer targetLedger,
		CanonicalTXSet& failedTransactions, bool final);

	// manipulating our own position
	void statusChange(newcoin::NodeEvent, Ledger& ledger);
	void takeInitialPosition(Ledger& initialLedger);
	void updateOurPositions();
	int getThreshold();
	void beginAccept();
	void endConsensus();

public:
	LedgerConsensus(const uint256& prevLCLHash, Ledger::pointer previousLedger, uint32 closeTime);

	int startup();
	Json::Value getJson();

	Ledger::pointer peekPreviousLedger()	{ return mPreviousLedger; }
	uint256 getLCL()						{ return mPrevLedgerHash; }

	SHAMap::pointer getTransactionTree(const uint256& hash, bool doAcquire);
	TransactionAcquire::pointer getAcquiring(const uint256& hash);
	void mapComplete(const uint256& hash, SHAMap::pointer map, bool acquired);
	void checkLCL();

	void timerEntry();

	// state handlers
	void statePreClose();
	void stateEstablish();
	void stateCutoff();
	void stateFinished();
	void stateAccepted();

	bool haveConsensus();

	bool peerPosition(LedgerProposal::pointer);

	bool peerHasSet(Peer::pointer peer, const uint256& set, newcoin::TxSetStatus status);

	bool peerGaveNodes(Peer::pointer peer, const uint256& setHash,
		const std::list<SHAMapNode>& nodeIDs, const std::list< std::vector<unsigned char> >& nodeData);
};


#endif
