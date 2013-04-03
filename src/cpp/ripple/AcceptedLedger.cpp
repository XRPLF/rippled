#include "AcceptedLedger.h"

#include <boost/foreach.hpp>

TaggedCache<uint256, AcceptedLedger> AcceptedLedger::ALCache("AcceptedLedger", 4, 60);

ALTransaction::ALTransaction(uint32 seq, SerializerIterator& sit)
{
	Serializer			txnSer(sit.getVL());
	SerializerIterator	txnIt(txnSer);

	mTxn =		boost::make_shared<SerializedTransaction>(boost::ref(txnIt));
	mRawMeta=	sit.getVL();
	mMeta =		boost::make_shared<TransactionMetaSet>(mTxn->getTransactionID(), seq, mRawMeta);
	mAffected =	mMeta->getAffectedAccounts();
	mResult =	mMeta->getResultTER();
	buildJson();
}

ALTransaction::ALTransaction(SerializedTransaction::ref txn, TransactionMetaSet::ref met) :
	mTxn(txn), mMeta(met), mAffected(met->getAffectedAccounts())
{
	mResult = mMeta->getResultTER();
	buildJson();
}

ALTransaction::ALTransaction(SerializedTransaction::ref txn, TER result) :
	mTxn(txn), mResult(result), mAffected(txn->getMentionedAccounts())
{
	buildJson();
}

std::string ALTransaction::getEscMeta() const
{
	assert(!mRawMeta.empty());
	return sqlEscape(mRawMeta);
}

void ALTransaction::buildJson()
{
	mJson = Json::objectValue;
	mJson["transaction"] = mTxn->getJson(0);
	if (mMeta)
	{
		mJson["meta"] = mMeta->getJson(0);
		mJson["raw_meta"] = strHex(mRawMeta);
	}
	mJson["result"] = transHuman(mResult);

	if (!mAffected.empty())
	{
		Json::Value affected(Json::arrayValue);
		BOOST_FOREACH(const RippleAddress& ra, mAffected)
		{
			affected.append(ra.humanAccountID());
		}
		mJson["affected"] = affected;
	}
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

AcceptedLedger::pointer AcceptedLedger::makeAcceptedLedger(Ledger::ref ledger)
{
	AcceptedLedger::pointer ret = ALCache.fetch(ledger->getHash());
	if (ret)
		return ret;
	ret = AcceptedLedger::pointer(new AcceptedLedger(ledger));
	ALCache.canonicalize(ledger->getHash(), ret);
	return ret;
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
