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

class LCPosition
{ // A position taken by one of our trusted peers
protected:
	uint256 mPubKeyHash;
	CKey::pointer mPubKey;
	uint256 mCurrentPosition;
	uint32 mSequence;

public:
	typedef boost::shared_ptr<LCPosition> pointer;

	LCPosition(CKey::pointer pubKey, const uint256& currentPosition, uint32 seq) :
		mPubKey(pubKey), mCurrentPosition(currentPosition), mSequence(seq) { ; }

	const uint256& getPubKeyHash() const		{ return mPubKeyHash; }
	const uint256& getCurrentPosition() const	{ return mCurrentPosition; }
	uint32 getSeq() const						{ return mSequence; }

	bool verifySignature(const uint256& hash, const std::vector<unsigned char>& signature) const;
	void setPosition(const uint256& position, uint32 sequence);
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

	// Convergence tracking, trusted peers indexed by hash of public key
	boost::unordered_map<uint256, LCPosition::pointer> mPeerPositions;

	// Transaction Sets, indexed by hash of transaction tree
	boost::unordered_map<uint256, SHAMap::pointer> mComplete;
	boost::unordered_map<uint256, TransactionAcquire::pointer> mAcquiring;

	// Peer sets
	boost::unordered_map<uint256, std::vector< boost::weak_ptr<Peer> > > mPeerData;

	void startup();
	void weHave(const uint256& id, Peer::pointer avoidPeer);

public:
	LedgerConsensus(Ledger::pointer previousLedger, Ledger::pointer currentLedger) :
		mPreviousLedger(previousLedger), mCurrentLedger(currentLedger)
	{ startup(); }

	Ledger::pointer peekPreviousLedger()	{ return mPreviousLedger; }
	Ledger::pointer peekCurrentLedger()		{ return mCurrentLedger; }

	LCPosition::pointer getCreatePeerPosition(const uint256& pubKeyHash);

	SHAMap::pointer getTransactionTree(const uint256& hash);
	TransactionAcquire::pointer getAcquiring(const uint256& hash);
	void acquireComplete(const uint256& hash);

	LCPosition::pointer getPeerPosition(const uint256& peer);

	// high-level functions
	void abort();
	bool peerPosition(Peer::pointer peer, const Serializer& report);
	bool peerHasSet(Peer::pointer peer, const std::vector<uint256>& sets);
	bool peerGaveNodes(Peer::pointer peer, const uint256& setHash,
		const std::list<SHAMapNode>& nodeIDs, const std::list< std::vector<unsigned char> >& nodeData);
};


#endif
