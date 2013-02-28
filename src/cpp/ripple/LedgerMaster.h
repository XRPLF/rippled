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
	Ledger::pointer mValidLedger;		// The highest-sequence ledger we have fully accepted
	Ledger::pointer mPubLedger;			// The last ledger we have published

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
	bool isValidTransaction(Transaction::ref trans);
	bool isTransactionOnFutureList(Transaction::ref trans);

	bool acquireMissingLedger(Ledger::ref from, const uint256& ledgerHash, uint32 ledgerSeq);
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
	Ledger::ref getCurrentLedger()		{ return mCurrentLedger; }

	// The finalized ledger is the last closed/accepted ledger
	Ledger::ref getClosedLedger()		{ return mFinalizedLedger; }

	// The published ledger is the last fully validated ledger
	Ledger::ref getValidatedLedger()	{ return mPubLedger; }

	TER doTransaction(const SerializedTransaction& txn, TransactionEngineParams params, bool& didApply);

	void pushLedger(Ledger::ref newLedger);
	void pushLedger(Ledger::ref newLCL, Ledger::ref newOL, bool fromConsensus);
	void storeLedger(Ledger::ref);

	void setFullLedger(Ledger::ref ledger);

	void switchLedgers(Ledger::ref lastClosed, Ledger::ref newCurrent);

	std::string getCompleteLedgers()
	{
		boost::recursive_mutex::scoped_lock sl(mLock);
		return mCompleteLedgers.toString();
	}

	Ledger::pointer closeLedger(bool recoverHeldTransactions);

	uint256 getHashBySeq(uint32 index)
	{
		uint256 hash = mLedgerHistory.getLedgerHash(index);
		if (hash.isNonZero())
			return hash;
		return Ledger::getHashByIndex(index);
	}

	Ledger::pointer getLedgerBySeq(uint32 index)
	{
		if (mCurrentLedger && (mCurrentLedger->getLedgerSeq() == index))
			return mCurrentLedger;
		if (mFinalizedLedger && (mFinalizedLedger->getLedgerSeq() == index))
			return mFinalizedLedger;
		Ledger::pointer ret = mLedgerHistory.getLedgerBySeq(index);
		if (ret)
			return ret;

		boost::recursive_mutex::scoped_lock ml(mLock);
		mCompleteLedgers.clearValue(index);
		return ret;
	}

	Ledger::pointer getLedgerByHash(const uint256& hash)
	{
		if (hash.isZero())
			return boost::make_shared<Ledger>(boost::ref(*mCurrentLedger), false);

		if (mCurrentLedger && (mCurrentLedger->getHash() == hash))
			return boost::make_shared<Ledger>(boost::ref(*mCurrentLedger), false);

		if (mFinalizedLedger && (mFinalizedLedger->getHash() == hash))
			return mFinalizedLedger;

		return mLedgerHistory.getLedgerByHash(hash);
	}

	void setLedgerRangePresent(uint32 minV, uint32 maxV)
	{
		boost::recursive_mutex::scoped_lock sl(mLock);
		mCompleteLedgers.setRange(minV, maxV);
	}

	void addHeldTransaction(Transaction::ref trans);
	void fixMismatch(Ledger::ref ledger);

	bool haveLedgerRange(uint32 from, uint32 to);
	bool haveLedger(uint32 seq);

	void resumeAcquiring();

	void tune(int size, int age) { mLedgerHistory.tune(size, age); } 
	void sweep(void) { mLedgerHistory.sweep(); }

	void addValidateCallback(callback& c) { mOnValidate.push_back(c); }

	void checkAccept(const uint256& hash);
	void checkAccept(const uint256& hash, uint32 seq);
	void tryPublish();

	static bool shouldAcquire(uint32 currentLedgerID, uint32 ledgerHistory, uint32 targetLedger);
};

#endif
// vim:ts=4
