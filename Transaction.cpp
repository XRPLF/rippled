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

Transaction::Transaction(TransStatus status, LocalAccount& fromLocalAccount, uint32 fromSeq,
		const uint160& toAccount, uint64 amount, uint32 ident, uint32 ledger) :
			mAccountTo(toAccount), mAmount(amount), mFromAccountSeq(fromSeq), mSourceLedger(ledger),
			mIdent(ident), mInLedger(0), mStatus(NEW)
{
	assert(fromLocalAccount.mAmount>=amount);
	mAccountFrom=fromLocalAccount.getAddress();
	updateFee();
	mFromPubKey.SetPubKey(fromLocalAccount.peekPubKey().GetPubKey());
	sign(fromLocalAccount);
}

Transaction::Transaction(const std::vector<unsigned char> &t, bool validate) : mStatus(INVALID)
{
	Serializer s(t);
	if(s.getLength()<145) return;
	if(!s.get160(mAccountTo, 0)) return;
	if(!s.get64(mAmount, 20)) return;
	if(!s.get32(mFromAccountSeq, 28)) return;
	if(!s.get32(mSourceLedger, 32)) return;
	if(!s.get32(mIdent, 36)) return;
	if(!s.getRaw(mSignature, 69, 72)) return;

	std::vector<unsigned char> pubKey;
	if(!s.getRaw(pubKey, 40, 33)) return;
	if(!mFromPubKey.SetPubKey(pubKey)) return;
	updateID();

	if(validate && !checkSign()) return;

	mStatus=NEW;
}

bool Transaction::sign(LocalAccount& fromLocalAccount)
{
	if( (mAmount==0) || (mSourceLedger==0) || (mAccountTo==0) )
		return false;
	if(mAccountFrom!=fromLocalAccount.mAddress)
		return false;
	Serializer::pointer signBuf=getRaw(true);
	if(!signBuf->makeSignature(mSignature, fromLocalAccount.peekPrivKey()))
	    return false;
	updateID();
	return true;
}

void Transaction::updateFee()
{ // for now, all transactions have a 1,000 unit fee
	mFee=1000;
}

bool Transaction::checkSign() const
{
	return mFromPubKey.Verify(getRaw(true)->getSHA512Half(), mSignature);
}

Serializer::pointer Transaction::getRaw(bool prefix) const
{
	Serializer::pointer ret(new Serializer(77));
	if(prefix) ret->add32(0x54584e00u);
	ret->addRaw(mFromPubKey.GetPubKey());
	ret->add64(mAmount);
	ret->add32(mFromAccountSeq);
	ret->add32(mInLedger);
	ret->add32(mIdent);
	assert( (prefix&&(ret->getLength()==77)) || (!prefix&&(ret->getLength()==73)) );
	return ret;
}

Serializer::pointer Transaction::getSigned() const
{
	Serializer::pointer ret(getRaw(false));
	ret->addRaw(mSignature);
	return ret;
}

void Transaction::updateID()
{
	mTransactionID=getSigned()->getSHA512Half();
}
