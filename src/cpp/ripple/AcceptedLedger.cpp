#include "AcceptedLedger.h"

ALTransaction::ALTransaction(uint32 seq, SerializerIterator& sit)
{
	Serializer			txnSer(sit.getVL());
	SerializerIterator	txnIt(txnSer);

	mTxn =		boost::make_shared<SerializedTransaction>(boost::ref(txnIt));
	mMeta =		boost::make_shared<TransactionMetaSet>(mTxn->getTransactionID(), seq, sit.getVL());
	mAffected =	mMeta->getAffectedAccounts();
}

AcceptedLedger::AcceptedLedger(Ledger::ref ledger) : mLedger(ledger)
{
	SHAMap& txSet = *ledger->peekTransactionMap();
	for (SHAMapItem::pointer item = txSet.peekFirstItem(); !!item; item = txSet.peekNextItem(item->getTag()))
	{
		SerializerIterator sit(item->peekSerializer());
		insert(ALTransaction(ledger->getLedgerSeq(), sit));
	}
}

void AcceptedLedger::insert(const ALTransaction& at)
{
	assert(mMap.find(at.getIndex()) == mMap.end());
	mMap.insert(std::make_pair(at.getIndex(), at));
}

const ALTransaction* AcceptedLedger::getTxn(int i) const
{
	map_t::const_iterator it = mMap.find(i);
	if (it == mMap.end())
		return NULL;
	return &it->second;
}