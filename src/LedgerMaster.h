#ifndef __LEDGERMASTER__
#define __LEDGERMASTER__

#include "Ledger.h"
#include "LedgerHistory.h"
#include "Peer.h"
#include "types.h"
#include "Transaction.h"
#include "TransactionEngine.h"

// Tracks the current ledger and any ledgers in the process of closing
// Tracks ledger history
// Tracks held transactions

class LedgerMaster
{
	boost::recursive_mutex mLock;

	TransactionEngine mEngine;

	Ledger::pointer mCurrentLedger;		// The ledger we are currently processiong
	Ledger::pointer mWobbleLedger;		// A ledger past its close time
	Ledger::pointer mFinalizedLedger;	// The ledger that most recently closed

	LedgerHistory mLedgerHistory;

	std::map<uint256, Transaction::pointer> mHeldTransactionsByID;

	void applyFutureTransactions(uint32 ledgerIndex);
	bool isValidTransaction(Transaction::pointer trans);
	bool isTransactionOnFutureList(Transaction::pointer trans);

public:

	LedgerMaster()						{ ; }

	uint32 getCurrentLedgerIndex();

	ScopedLock getLock()				{ return ScopedLock(mLock); }

	// The current ledger is the ledger we believe new transactions should go in
	Ledger::pointer getCurrentLedger()	{ return mCurrentLedger; }

	// The wobble ledger is a ledger that new transactions can go in if requested
	Ledger::pointer getWobbleLedger()	{ return mWobbleLedger; }

	// The finalized ledger is the last closed/accepted ledger
	Ledger::pointer getClosedLedger()	{ return mFinalizedLedger; }

	TransactionEngineResult doTransaction(const SerializedTransaction& txn, uint32 targetLedger,
		TransactionEngineParams params);

	void pushLedger(Ledger::pointer newLedger);
	void pushLedger(Ledger::pointer newLCL, Ledger::pointer newOL);
	void storeLedger(Ledger::pointer);

	void switchLedgers(Ledger::pointer lastClosed, Ledger::pointer newCurrent);

	void closeTime();
	void beginWobble();
	Ledger::pointer endWobble();

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
		if (mCurrentLedger && (mCurrentLedger->getHash() == hash))
			return mCurrentLedger;
		if (mFinalizedLedger && (mFinalizedLedger->getHash() == hash))
			return mFinalizedLedger;
		return mLedgerHistory.getLedgerByHash(hash);
	}

	bool addHeldTransaction(Transaction::pointer trans);
};

#endif
// vim:ts=4
