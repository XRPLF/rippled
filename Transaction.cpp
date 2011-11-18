#include "Transaction.h"
#include "Wallet.h"
#include "Account.h"
#include "BitcoinUtil.h"

using namespace std;

Transaction::Transaction() : mTransactionID(0), mAccountFrom(0), mAccountTo(0),
	mAmount(0), mFromAccountSeq(0), mSourceLedger(0), mIdent(0),
	mInLedger(0), mStatus(INVALID)
{
}

Transaction::Transaction(TransStatus status, LocalAccount& fromLocalAccount, Account& fromAccount,
		uint32 fromSeq,	const uint160& toAccount, uint64 amount, uint32 ident, uint32 ledger) :
		mAccountTo(toAccount), mAmount(amount), mFromAccountSeq(fromSeq), mSourceLedger(ledger),
		mIdent(ident), mInLedger(0), mStatus(NEW)
{
	assert(fromAccount.GetAddress()==fromLocalAccount.mAddress);
	assert(fromLocalAccount.mAmount>=amount);
	assert((fromSeq+1)==fromLocalAccount.mSeqNum);

	mAccountFrom=fromAccount.GetAddress();
	sign(fromLocalAccount, fromAccount);
}

bool Transaction::sign(LocalAccount& fromLocalAccount, Account& fromAccount)
{
	if( (mAmount==0) || (mSourceLedger==0) || (mAccountTo==0) )
		return false;
	if((mAccountFrom!=fromLocalAccount.mAddress)||(mAccountFrom!=fromAccount.GetAddress()))
		return false;
	
	Serializer::pointer signBuf(getRawUnsigned(fromAccount));
	if(!signBuf->makeSignature(mSignature, fromLocalAccount.peekPrivKey()))
	    return false;
    signBuf->addRaw(mSignature);
    mTransactionID=signBuf->getSHA512Half();
}

bool Transaction::checkSign(Account& fromAccount) const
{
	if(mAccountFrom!=fromAccount.GetAddress()) return false;

	Serializer::pointer toSign(getRawUnsigned(fromAccount));
	return toSign->checkSignature(mSignature, fromAccount.peekPubKey());
}

Serializer::pointer Transaction::getRawUnsigned(Account& fromAccount) const
{
	Serializer::pointer ret(new Serializer(104));
	ret->add32(0x54584e00u);
	ret->addRaw(fromAccount.peekPubKey().GetPubKey());
	ret->add64(mAmount);
	ret->add32(mFromAccountSeq);
	ret->add32(mInLedger);
	ret->add32(mIdent);
	return ret;
}

void Transaction::updateID(Account& fromAccount)
{
	mTransactionID=getRawSigned(fromAccount)->getSHA512Half();
}
