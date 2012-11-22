
#include "TransactionMaster.h"

#include <boost/bind.hpp>

#include "Application.h"

#ifndef CACHED_TRANSACTION_NUM
#define CACHED_TRANSACTION_NUM 65536
#endif

#ifndef CACHED_TRANSACTION_AGE
#define CACHED_TRANSACTION_AGE 1800
#endif

TransactionMaster::TransactionMaster() : mCache("TransactionCache", CACHED_TRANSACTION_NUM, CACHED_TRANSACTION_AGE)
{
	;
}

Transaction::pointer TransactionMaster::fetch(const uint256& txnID, bool checkDisk)
{
	Transaction::pointer txn = mCache.fetch(txnID);
	if (!checkDisk || txn) return txn;

	txn = Transaction::load(txnID);
	if (!txn) return txn;

	mCache.canonicalize(txnID, txn);

	return txn;
}

SerializedTransaction::pointer TransactionMaster::fetch(SHAMapItem::ref item, SHAMapTreeNode::TNType type,
	bool checkDisk, uint32 uCommitLedger)
{
	SerializedTransaction::pointer	txn;
	Transaction::pointer			iTx = theApp->getMasterTransaction().fetch(item->getTag(), false);

	if (!iTx)
	{

		if (type == SHAMapTreeNode::tnTRANSACTION_NM)
		{
			SerializerIterator sit(item->peekSerializer());
			txn = boost::make_shared<SerializedTransaction>(boost::ref(sit));
		}
		else if (type == SHAMapTreeNode::tnTRANSACTION_MD)
		{
			Serializer s;
			int length;
			item->peekSerializer().getVL(s.modData(), 0, length);
			SerializerIterator sit(s);

			txn = boost::make_shared<SerializedTransaction>(boost::ref(sit));
		}
	}
	else
	{
		if (uCommitLedger)
			iTx->setStatus(COMMITTED, uCommitLedger);

		txn = iTx->getSTransaction();
	}

	return txn;
}

static void saveTransactionHelper(Transaction::pointer txn, LoadEvent::pointer)
{
	Transaction::saveTransaction(txn);
}

bool TransactionMaster::canonicalize(Transaction::pointer& txn, bool may_be_new)
{
	uint256 tid = txn->getID();
	if (!tid) return false;
	if (mCache.canonicalize(tid, txn)) return true;
	if (may_be_new)
		theApp->getAuxService().post(boost::bind(&saveTransactionHelper, txn,
			theApp->getJobQueue().getLoadEvent(jtDISK)));
	return false;
}
// vim:ts=4
