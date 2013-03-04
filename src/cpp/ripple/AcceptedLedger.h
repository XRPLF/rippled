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
	std::vector<RippleAddress>		mAffected;

public:

	ALTransaction(uint32 ledgerSeq, SerializerIterator& sit);

	SerializedTransaction::ref getTxn()	const				{ return mTxn; }
	TransactionMetaSet::ref getMeta() const					{ return mMeta; }
	const std::vector<RippleAddress>& getAffected() const	{ return mAffected; }
	int getIndex() const									{ return mMeta->getIndex(); }
	TER getResult() const									{ return mMeta->getResultTER(); }
};

class AcceptedLedger
{
public:
	typedef std::map<int, ALTransaction>	map_t;
	typedef map_t::value_type				value_type;
	typedef map_t::const_iterator			const_iterator;

protected:
	Ledger::pointer		mLedger;
	map_t				mMap;

	void insert(const ALTransaction&);

public:
	AcceptedLedger(Ledger::ref ledger);

	Ledger::ref getLedger() const	{ return mLedger; }
	const map_t& getMap() const		{ return mMap; }

	int getLedgerSeq() const		{ return mLedger->getLedgerSeq(); }
	int getTxnCount() const			{ return mMap.size(); }

	const ALTransaction* getTxn(int) const;
};

#endif
