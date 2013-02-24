#ifndef __TRANSACTOR__
#define __TRANSACTOR__

#include "SerializedTransaction.h"
#include "TransactionErr.h"
#include "TransactionEngine.h"
#include <boost/shared_ptr.hpp>

class Transactor
{
protected:
	const SerializedTransaction&	mTxn;
	TransactionEngine*				mEngine;
	TransactionEngineParams			mParams;

	uint160							mTxnAccountID;
	STAmount						mFeeDue;
	STAmount						mPriorBalance;	// Balance before fees.
	STAmount						mSourceBalance;	// Balance after fees.
	SLE::pointer					mTxnAccount;
	bool							mHasAuthKey;
	RippleAddress					mSigningPubKey;

	TER preCheck();
	TER checkSeq();
	TER payFee();

	void calculateFee();

	// Returns the fee, not scaled for load (Should be in fee units. FIXME)
	virtual uint64 calculateBaseFee();

	virtual TER checkSig();
	virtual TER doApply()=0;

	Transactor(const SerializedTransaction& txn, TransactionEngineParams params, TransactionEngine* engine);

public:
	typedef boost::shared_ptr<Transactor> pointer;

	static std::auto_ptr<Transactor> makeTransactor(const SerializedTransaction& txn,TransactionEngineParams params, TransactionEngine* engine);

	TER apply();
};

#endif

// vim:ts=4
