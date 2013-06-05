#ifndef __LEDGER_CONSENSUS__
#define __LEDGER_CONSENSUS__

#include <list>
#include <map>

#include <boost/weak_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/unordered_map.hpp>

#include "Transaction.h"
#include "LedgerAcquire.h"
#include "LedgerProposal.h"
#include "CanonicalTXSet.h"
#include "TransactionEngine.h"

DEFINE_INSTANCE(LedgerConsensus);
DEFINE_INSTANCE(TransactionAcquire);

// A transaction set we are trying to acquire
class TransactionAcquire
    : private IS_INSTANCE (TransactionAcquire)
    , public PeerSet
    , public boost::enable_shared_from_this <TransactionAcquire>
{
public:
	typedef boost::shared_ptr<TransactionAcquire> pointer;

public:
	TransactionAcquire(const uint256& hash);
	virtual ~TransactionAcquire()		{ ; }

	SHAMap::ref getMap()				{ return mMap; }

	SMAddNode takeNodes(const std::list<SHAMapNode>& IDs,
		const std::list< std::vector<unsigned char> >& data, Peer::ref);

private:
	SHAMap::pointer		mMap;
	bool				mHaveRoot;

	void onTimer(bool progress);
	void newPeer(Peer::ref peer)	{ trigger(peer); }

	void done();
	void trigger(Peer::ref);
	boost::weak_ptr<PeerSet> pmDowncast();
};

// A transaction that may be disputed
class LCTransaction
{
public:
	typedef boost::shared_ptr<LCTransaction> pointer;

	LCTransaction(const uint256 &txID, const std::vector<unsigned char>& tx, bool ourVote) :
		mTransactionID(txID), mYays(0), mNays(0), mOurVote(ourVote), transaction(tx) { ; }

	const uint256& getTransactionID() const				{ return mTransactionID; }
	bool getOurVote() const								{ return mOurVote; }
	Serializer& peekTransaction()						{ return transaction; }
	void setOurVote(bool o)								{ mOurVote = o; }

	void setVote(const uint160& peer, bool votesYes);
	void unVote(const uint160& peer);

	bool updateVote(int percentTime, bool proposing);
	Json::Value getJson();

private:
	uint256 mTransactionID;
	int mYays, mNays;
	bool mOurVote;
	Serializer transaction;
	boost::unordered_map<uint160, bool> mVotes;
};

enum LCState
{
	lcsPRE_CLOSE,		// We haven't closed our ledger yet, but others might have
	lcsESTABLISH,		// Establishing consensus
	lcsFINISHED,		// We have closed on a transaction set
	lcsACCEPTED,		// We have accepted/validated a new last closed ledger
};

class LedgerConsensus : public boost::enable_shared_from_this<LedgerConsensus>, IS_INSTANCE(LedgerConsensus)
{
public:
	LedgerConsensus(const uint256& prevLCLHash, Ledger::ref previousLedger, uint32 closeTime);

	int startup();
	Json::Value getJson(bool full);

	Ledger::ref peekPreviousLedger()	{ return mPreviousLedger; }
	uint256 getLCL()					{ return mPrevLedgerHash; }

	SHAMap::pointer getTransactionTree(const uint256& hash, bool doAcquire);
	TransactionAcquire::pointer getAcquiring(const uint256& hash);
	void mapComplete(const uint256& hash, SHAMap::ref map, bool acquired);
	bool stillNeedTXSet(const uint256& hash);
	void checkLCL();
	void handleLCL(const uint256& lclHash);

	void timerEntry();

	// state handlers
	void statePreClose();
	void stateEstablish();
	void stateCutoff();
	void stateFinished();
	void stateAccepted();

	bool haveConsensus(bool forReal);

	bool peerPosition(LedgerProposal::ref);

	bool peerHasSet(Peer::ref peer, const uint256& set, ripple::TxSetStatus status);

	SMAddNode peerGaveNodes(Peer::ref peer, const uint256& setHash,
		const std::list<SHAMapNode>& nodeIDs, const std::list< std::vector<unsigned char> >& nodeData);

	bool isOurPubKey(const RippleAddress &k)	{ return k == mValPublic; }

	// test/debug
	void simulate();

private:
	// final accept logic
	void accept(SHAMap::ref txSet, LoadEvent::pointer);

	void weHave(const uint256& id, Peer::ref avoidPeer);
	void startAcquiring(TransactionAcquire::pointer);
	SHAMap::pointer find(const uint256& hash);

	void createDisputes(SHAMap::ref, SHAMap::ref);
	void addDisputedTransaction(const uint256&, const std::vector<unsigned char>& transaction);
	void adjustCount(SHAMap::ref map, const std::vector<uint160>& peers);
	void propose();

	void addPosition(LedgerProposal&, bool ours);
	void removePosition(LedgerProposal&, bool ours);
	void sendHaveTxSet(const uint256& set, bool direct);
	void applyTransactions(SHAMap::ref transactionSet, Ledger::ref targetLedger,
		Ledger::ref checkLedger, CanonicalTXSet& failedTransactions, bool openLgr);
	int applyTransaction(TransactionEngine& engine, SerializedTransaction::ref txn, Ledger::ref targetLedger,
		bool openLgr, bool retryAssured);

	uint32 roundCloseTime(uint32 closeTime);

	// manipulating our own position
	void statusChange(ripple::NodeEvent, Ledger& ledger);
	void takeInitialPosition(Ledger& initialLedger);
	void updateOurPositions();
	void playbackProposals();
	int getThreshold();
	void closeLedger();
	void checkOurValidation();

	void beginAccept(bool synchronous);
	void endConsensus();

private:
	LCState mState;
	uint32 mCloseTime;						// The wall time this ledger closed
	uint256 mPrevLedgerHash, mNewLedgerHash;
	Ledger::pointer mPreviousLedger;
	LedgerAcquire::pointer mAcquiringLedger;
	LedgerProposal::pointer mOurPosition;
	RippleAddress mValPublic, mValPrivate;
	bool mProposing, mValidating, mHaveCorrectLCL, mConsensusFail;

	int mCurrentMSeconds, mClosePercent, mCloseResolution;
	bool mHaveCloseTimeConsensus;

	boost::posix_time::ptime		mConsensusStartTime;
	int								mPreviousProposers;
	int								mPreviousMSeconds;

	// Convergence tracking, trusted peers indexed by hash of public key
	boost::unordered_map<uint160, LedgerProposal::pointer> mPeerPositions;

	// Transaction Sets, indexed by hash of transaction tree
	boost::unordered_map<uint256, SHAMap::pointer> mAcquired;
	boost::unordered_map<uint256, TransactionAcquire::pointer> mAcquiring;

	// Peer sets
	boost::unordered_map<uint256, std::vector< boost::weak_ptr<Peer> > > mPeerData;

	// Disputed transactions
	boost::unordered_map<uint256, LCTransaction::pointer> mDisputes;

	// Close time estimates
	std::map<uint32, int> mCloseTimes;

	// nodes that have bowed out of this consensus process
	boost::unordered_set<uint160> mDeadNodes;
};


#endif
