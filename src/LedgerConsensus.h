#ifndef __LEDGER_CONSENSUS__
#define __LEDGER_CONSENSUS__

#include <list>

#include <boost/weak_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/unordered/unordered_map.hpp>

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
	THSyncFilter		mFilter; // FIXME: Should use transaction master too
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

	bool updatePosition(int timePassed);
	int getAgreeLevel();
};

enum LCState
{
	lcsPRE_CLOSE,		// We haven't closed our ledger yet, but others might have
	lcsPOST_CLOSE,		// Ledger closed, but wobble time
	lcsESTABLISH,		// Establishing consensus
	lcsCUTOFF,			// Past the cutoff for consensus
	lcsFINISHED,		// We have closed on a transaction set
	lcsACCEPTED,		// We have accepted/validated a new last closed ledger
	lcsABORTED			// Abandoned
};

class LedgerConsensus : public boost::enable_shared_from_this<LedgerConsensus>
{
protected:
	LCState mState;
	uint32 mCloseTime;
	Ledger::pointer mPreviousLedger;
	LedgerProposal::pointer mOurPosition;
	bool mProposing, mValidating;

	// Convergence tracking, trusted peers indexed by hash of public key
	boost::unordered_map<uint160, LedgerProposal::pointer> mPeerPositions;

	// Transaction Sets, indexed by hash of transaction tree
	boost::unordered_map<uint256, SHAMap::pointer> mComplete;
	boost::unordered_map<uint256, TransactionAcquire::pointer> mAcquiring;

	// Peer sets
	boost::unordered_map<uint256, std::vector< boost::weak_ptr<Peer> > > mPeerData;

	// Disputed transactions
	boost::unordered_map<uint256, LCTransaction::pointer> mDisputes;

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
	void applyTransactions(SHAMap::pointer transactionSet, Ledger::pointer targetLedger,
		CanonicalTXSet& failedTransactions, bool final);
	void applyTransaction(TransactionEngine& engine, SerializedTransaction::pointer txn, Ledger::pointer targetLedger,
		CanonicalTXSet& failedTransactions, bool final);

	// manipulating our own position
	void takeInitialPosition(Ledger::pointer initialLedger);
	bool updateOurPositions(int sinceClose);
	void statusChange(newcoin::NodeEvent, Ledger::pointer ledger);
	int getThreshold();
	void beginAccept();
	void endConsensus();

public:
	LedgerConsensus(Ledger::pointer previousLedger, uint32 closeTime);

	int startup();

	Ledger::pointer peekPreviousLedger()	{ return mPreviousLedger; }
	uint256 getLCL()						{ return mPreviousLedger->getHash(); }

	SHAMap::pointer getTransactionTree(const uint256& hash, bool doAcquire);
	TransactionAcquire::pointer getAcquiring(const uint256& hash);
	void mapComplete(const uint256& hash, SHAMap::pointer map, bool acquired);

	void abort();
	int timerEntry();

	// state handlers
	int statePreClose(int secondsSinceClose);
	int statePostClose(int secondsSinceClose);
	int stateEstablish(int secondsSinceClose);
	int stateCutoff(int secondsSinceClose);
	int stateFinished(int secondsSinceClose);
	int stateAccepted(int secondsSinceClose);

	bool peerPosition(LedgerProposal::pointer);

	bool peerHasSet(Peer::pointer peer, const uint256& set, newcoin::TxSetStatus status);

	bool peerGaveNodes(Peer::pointer peer, const uint256& setHash,
		const std::list<SHAMapNode>& nodeIDs, const std::list< std::vector<unsigned char> >& nodeData);
};


#endif
