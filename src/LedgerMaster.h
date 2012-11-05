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
	boost::recursive_mutex mLock;

	TransactionEngine mEngine;

	Ledger::pointer mCurrentLedger;		// The ledger we are currently processiong
	Ledger::pointer mFinalizedLedger;	// The ledger that most recently closed

	LedgerHistory mLedgerHistory;

	CanonicalTXSet mHeldTransactions;

	RangeSet mCompleteLedgers;
	LedgerAcquire::pointer mMissingLedger;
	uint32 mMissingSeq;

	void applyFutureTransactions(uint32 ledgerIndex);
	bool isValidTransaction(const Transaction::pointer& trans);
	bool isTransactionOnFutureList(const Transaction::pointer& trans);

	void acquireMissingLedger(const uint256& ledgerHash, uint32 ledgerSeq);
	void missingAcquireComplete(LedgerAcquire::pointer);

public:

	LedgerMaster() : mHeldTransactions(uint256()), mMissingSeq(0)	{ ; }

	uint32 getCurrentLedgerIndex();

	ScopedLock getLock()				{ return ScopedLock(mLock); }

	// The current ledger is the ledger we believe new transactions should go in
	Ledger::pointer getCurrentLedger()	{ return mCurrentLedger; }

	// The finalized ledger is the last closed/accepted ledger
	Ledger::pointer getClosedLedger()	{ return mFinalizedLedger; }

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

	void sweep(void) { mLedgerHistory.sweep(); }
};

#endif
// vim:ts=4
