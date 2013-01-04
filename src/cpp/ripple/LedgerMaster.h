#ifndef __LEDGERMASTER__
#define __LEDGERMASTER__

#include "Ledger.h"
#include "LedgerHistory.h"
#include "Peer.h"
#include "types.h"
#include "LedgerAcquire.h"
#include "Transaction.h"
#include "TransactionEngine.h"
#include "RangeSet.h"
#include "CanonicalTXSet.h"

// Tracks the current ledger and any ledgers in the process of closing
// Tracks ledger history
// Tracks held transactions

class LedgerMaster
{
public:
	typedef boost::function<void(Ledger::ref)> callback;

protected:
	boost::recursive_mutex mLock;

	TransactionEngine mEngine;

	Ledger::pointer mCurrentLedger;		// The ledger we are currently processiong
	Ledger::pointer mFinalizedLedger;	// The ledger that most recently closed
	Ledger::pointer mValidLedger;		// The ledger we most recently fully accepted

	LedgerHistory mLedgerHistory;

	CanonicalTXSet mHeldTransactions;

	RangeSet mCompleteLedgers;
	LedgerAcquire::pointer mMissingLedger;
	uint32 mMissingSeq;
	bool mTooFast;	// We are acquiring faster than we're writing

	int							mMinValidations;	// The minimum validations to publish a ledger
	uint256						mLastValidateHash;
	uint32						mLastValidateSeq;
	std::list<callback>			mOnValidate;		// Called when a ledger has enough validations

	std::list<Ledger::pointer>	mPubLedgers;		// List of ledgers to publish
	bool						mPubThread;			// Publish thread is running

	void applyFutureTransactions(uint32 ledgerIndex);
	bool isValidTransaction(const Transaction::pointer& trans);
	bool isTransactionOnFutureList(const Transaction::pointer& trans);

	void acquireMissingLedger(const uint256& ledgerHash, uint32 ledgerSeq);
	void asyncAccept(Ledger::pointer);
	void missingAcquireComplete(LedgerAcquire::pointer);
	void pubThread();

public:

	LedgerMaster() : mHeldTransactions(uint256()), mMissingSeq(0), mTooFast(false),
		mMinValidations(0), mLastValidateSeq(0), mPubThread(false)
	{ ; }

	uint32 getCurrentLedgerIndex();

	ScopedLock getLock()				{ return ScopedLock(mLock); }

	// The current ledger is the ledger we believe new transactions should go in
	Ledger::ref getCurrentLedger()	{ return mCurrentLedger; }

	// The finalized ledger is the last closed/accepted ledger
	Ledger::ref getClosedLedger()	{ return mFinalizedLedger; }

	TER doTransaction(const SerializedTransaction& txn, TransactionEngineParams params);

	void pushLedger(Ledger::ref newLedger);
	void pushLedger(Ledger::ref newLCL, Ledger::ref newOL, bool fromConsensus);
	void storeLedger(Ledger::ref);

	void setFullLedger(Ledger::ref ledger);

	void switchLedgers(Ledger::ref lastClosed, Ledger::ref newCurrent);

	std::string getCompleteLedgers()	{ return mCompleteLedgers.toString(); }

	Ledger::pointer closeLedger(bool recoverHeldTransactions);

	Ledger::pointer getLedgerBySeq(uint32 index)
	{
		if (mCurrentLedger && (mCurrentLedger->getLedgerSeq() == index))
			return mCurrentLedger;
		if (mFinalizedLedger && (mFinalizedLedger->getLedgerSeq() == index))
			return mFinalizedLedger;
		return mLedgerHistory.getLedgerBySeq(index);
	}

	Ledger::pointer getLedgerByHash(const uint256& hash)
	{
		if (hash.isZero())
			return mCurrentLedger;

		if (mCurrentLedger && (mCurrentLedger->getHash() == hash))
			return mCurrentLedger;
		if (mFinalizedLedger && (mFinalizedLedger->getHash() == hash))
			return mFinalizedLedger;

		return mLedgerHistory.getLedgerByHash(hash);
	}

	void setLedgerRangePresent(uint32 minV, uint32 maxV) { mCompleteLedgers.setRange(minV, maxV); }

	void addHeldTransaction(const Transaction::pointer& trans);
	void fixMismatch(Ledger::ref ledger);

	bool haveLedgerRange(uint32 from, uint32 to);

	void resumeAcquiring();

	void sweep(void) { mLedgerHistory.sweep(); }

	void addValidateCallback(callback& c) { mOnValidate.push_back(c); }

	void checkPublish(const uint256& hash);
	void checkPublish(const uint256& hash, uint32 seq);
};

#endif
// vim:ts=4
