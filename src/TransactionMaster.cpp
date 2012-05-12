
#include "boost/bind.hpp"

#include "Application.h"
#include "TransactionMaster.h"

#ifndef CACHED_TRANSACTION_NUM
#define CACHED_TRANSACTION_NUM 65536
#endif

#ifndef CACHED_TRANSACTION_AGE
#define CACHED_TRANSACTION_AGE 1800
#endif

TransactionMaster::TransactionMaster() : mCache(CACHED_TRANSACTION_NUM, CACHED_TRANSACTION_AGE)
{
	;
}

Transaction::pointer TransactionMaster::fetch(const uint256& txnID, bool checkDisk)
{
	Transaction::pointer txn=mCache.fetch(txnID);
	if(!checkDisk || txn) return txn;

	txn=Transaction::load(txnID);
	if(!txn) return txn;

	mCache.canonicalize(txnID, txn);
	return txn;
}

bool TransactionMaster::canonicalize(Transaction::pointer& txn, bool may_be_new)
{
	uint256 tid=txn->getID();
	if(!tid) return false;
	if(mCache.canonicalize(tid, txn)) return true;
	if(may_be_new)
		theApp->getIOService().post(boost::bind(&Transaction::saveTransaction, txn));
	return false;
}
// vim:ts=4
