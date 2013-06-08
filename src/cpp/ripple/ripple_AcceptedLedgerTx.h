#ifndef RIPPLE_ACCEPTEDLEDGERTX_H
#define RIPPLE_ACCEPTEDLEDGERTX_H

/*============================================================================*/
/**
    A transaction that is in a closed ledger.

    Description

	@code
    @endcode

    @see {uri}

    @ingroup ripple_ledger
*/
class AcceptedLedgerTx
{
public:
	typedef boost::shared_ptr<AcceptedLedgerTx> pointer;
	typedef const pointer& ref;

public:
	AcceptedLedgerTx (LedgerIndex ledgerSeq, SerializerIterator& sit);
	AcceptedLedgerTx (SerializedTransaction::ref, TransactionMetaSet::ref);
	AcceptedLedgerTx (SerializedTransaction::ref, TER result);

	SerializedTransaction::ref getTxn()	const				{ return mTxn; }
	TransactionMetaSet::ref getMeta() const					{ return mMeta; }
	std::vector <RippleAddress> const& getAffected() const	{ return mAffected; }

	TxID getTransactionID() const						    { return mTxn->getTransactionID(); }
	TransactionType getTxnType() const						{ return mTxn->getTxnType(); }
	TER getResult() const									{ return mResult; }
	uint32 getTxnSeq() const								{ return mMeta->getIndex(); }

	bool isApplied() const									{ return !!mMeta; }
	int getIndex() const									{ return mMeta ? mMeta->getIndex() : 0; }
	std::string getEscMeta() const;
	Json::Value getJson() const								{ return mJson; }

private:
	SerializedTransaction::pointer	mTxn;
	TransactionMetaSet::pointer		mMeta;
	TER								mResult;
	std::vector <RippleAddress>		mAffected;
	Blob 		mRawMeta;
	Json::Value						mJson;

	void buildJson();
};

#endif
