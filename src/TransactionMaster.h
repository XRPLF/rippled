#ifndef __TRANSACTIONMASTER__
#define __TRANSACTIONMASTER__

#include "Transaction.h"
#include "TaggedCache.h"

// Tracks all transactions in memory

class TransactionMaster
{
protected:
	TaggedCache<uint256, Transaction> mCache;

public:

	TransactionMaster();

	Transaction::pointer			fetch(const uint256&, bool checkDisk);
	SerializedTransaction::pointer	fetch(const SHAMapItem::pointer& item, bool checkDisk, uint32 uCommitLedger);

	// return value: true = we had the transaction already
	bool canonicalize(Transaction::pointer& txn, bool maybeNew);
};

#endif
// vim:ts=4
