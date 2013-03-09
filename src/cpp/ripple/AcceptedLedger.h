#ifndef ACCEPTED_LEDGER__H
#define ACCEPTED_LEDGER__H

#include "SerializedTransaction.h"
#include "TransactionMeta.h"
#include "Ledger.h"


class ALTransaction
{
protected:
	SerializedTransaction::pointer	mTxn;
	TransactionMetaSet::pointer		mMeta;
	TER								mResult;
	std::vector<RippleAddress>		mAffected;
	std::vector<unsigned char>		mRawMeta;

public:

	ALTransaction(uint32 ledgerSeq, SerializerIterator& sit);
	ALTransaction(SerializedTransaction::ref, TransactionMetaSet::ref);
	ALTransaction(SerializedTransaction::ref, TER result);

	SerializedTransaction::ref getTxn()	const				{ return mTxn; }
	TransactionMetaSet::ref getMeta() const					{ return mMeta; }
	const std::vector<RippleAddress>& getAffected() const	{ return mAffected; }

	uint256 getTransactionID() const						{ return mTxn->getTransactionID(); }
	TransactionType getTxnType() const						{ return mTxn->getTxnType(); }
	TER getResult() const									{ return mResult; }

	bool isApplied() const									{ return !!mMeta; }
	int getIndex() const									{ return mMeta ? mMeta->getIndex() : 0; }
	std::string getEscMeta() const;
	Json::Value getJson(int) const;
};

class AcceptedLedger
{
public:
	typedef boost::shared_ptr<AcceptedLedger>	pointer;
	typedef const pointer&						ret;
	typedef std::map<int, ALTransaction>		map_t;				// Must be an ordered map!
	typedef map_t::value_type					value_type;
	typedef map_t::const_iterator				const_iterator;

protected:
	Ledger::pointer		mLedger;
	map_t				mMap;

	void insert(const ALTransaction&);

	static TaggedCache<uint256, AcceptedLedger>	ALCache;
	AcceptedLedger(Ledger::ref ledger);

public:

	static pointer makeAcceptedLedger(Ledger::ref ledger);
	static void sweep()				{ ALCache.sweep(); }

	Ledger::ref getLedger() const	{ return mLedger; }
	const map_t& getMap() const		{ return mMap; }

	int getLedgerSeq() const		{ return mLedger->getLedgerSeq(); }
	int getTxnCount() const			{ return mMap.size(); }

	const ALTransaction* getTxn(int) const;
};

#endif
