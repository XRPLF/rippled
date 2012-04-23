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
	bool mIsSynced;

	TransactionEngine mEngine;

	Ledger::pointer mCurrentLedger;
	Ledger::pointer mFinalizingLedger;

	LedgerHistory mLedgerHistory;

	std::map<uint256, Transaction::pointer> mHeldTransactionsByID;

	void applyFutureTransactions(uint32 ledgerIndex);
	bool isValidTransaction(Transaction::pointer trans);
	bool isTransactionOnFutureList(Transaction::pointer trans);

public:

	LedgerMaster();

	uint32 getCurrentLedgerIndex();
	bool isSynced() { return mIsSynced; }
	void setSynced() { mIsSynced=true; }

	Ledger::pointer getCurrentLedger() { return mCurrentLedger; }
	Ledger::pointer getClosingLedger() { return mFinalizingLedger; }

	TransactionEngineResult doTransaction(const SerializedTransaction& txn, TransactionEngineParams params)
	{ return mEngine.applyTransaction(txn, params); }

	void pushLedger(Ledger::pointer newLedger);

	Ledger::pointer getLedgerBySeq(uint32 index)
	{
		if(mCurrentLedger && (mCurrentLedger->getLedgerSeq()==index)) return mCurrentLedger;
		if(mFinalizingLedger && (mFinalizingLedger->getLedgerSeq()==index)) return mFinalizingLedger;
		return mLedgerHistory.getLedgerBySeq(index); 
	}

	Ledger::pointer getLedgerByHash(const uint256& hash)
	{
		if(mCurrentLedger && (mCurrentLedger->getHash()==hash)) return mCurrentLedger;
		if(mFinalizingLedger && (mFinalizingLedger->getHash()==hash)) return mFinalizingLedger;
		return mLedgerHistory.getLedgerByHash(hash);
	}

	bool addHeldTransaction(Transaction::pointer trans);
	uint64 getBalance(std::string& strAcctID, const uint160 currency = 0);
	uint64 getBalance(const NewcoinAddress& acctID, const uint160 currency = 0);
};

#endif
