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

	Ledger::pointer getCurrentLedger()	{ return mCurrentLedger; }
	Ledger::pointer getClosedLedger()	{ return mFinalizedLedger; }

	TransactionEngineResult doTransaction(const SerializedTransaction& txn, TransactionEngineParams params)
	{ return mEngine.applyTransaction(txn, params); }

	void pushLedger(Ledger::pointer newLedger);
	void pushLedger(Ledger::pointer newLCL, Ledger::pointer newOL);
	void switchLedgers(Ledger::pointer lastClosed, Ledger::pointer newCurrent);

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
		if (mCurrentLedger && (mCurrentLedger->getHash() == hash)) return mCurrentLedger;
		if (mFinalizedLedger && (mFinalizedLedger->getHash() == hash)) return mFinalizedLedger;
		return mLedgerHistory.getLedgerByHash(hash);
	}

	bool addHeldTransaction(Transaction::pointer trans);
};

#endif
// vim:ts=4
