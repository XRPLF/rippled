#ifndef __TRANSACTOR__
#define __TRANSACTOR__

#include "SerializedTransaction.h"
#include "TransactionErr.h"
#include "TransactionEngine.h"
#include <boost/shared_ptr.hpp>

class Transactor
{
protected:
	const SerializedTransaction& mTxn;
	TransactionEngine::pointer mEngine;
	TransactionEngineParams mParams;

	uint160 mTxnAccountID;
	STAmount mFeeDue;
	STAmount mSourceBalance;
	SLE::pointer mTxnAccount;
	bool mHasAuthKey;
	RippleAddress mSigningPubKey;
	

	TER preCheck();
	TER checkSeq();
	TER payFee();
	virtual void calculateFee();
	virtual TER checkSig();
	virtual TER doApply()=0;
	
	Transactor(const SerializedTransaction& txn, TransactionEngineParams params, TransactionEngine::pointer engine);

public:
	typedef boost::shared_ptr<Transactor> pointer;

	static Transactor::pointer makeTransactor(const SerializedTransaction& txn,TransactionEngineParams params, TransactionEngine::pointer engine);

	TER apply();
};

#endif