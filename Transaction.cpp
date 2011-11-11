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

Transaction::Transaction(TransStatus status, LocalAccount &fromLocalAccount, const Account &fromAccount,
        uint32 fromSeq,	const uint160 &toAccount, uint64 amount, uint32 ident, uint32 ledger) :
	    mAccountTo(toAccount), mAmount(amount), mFromAccountSeq(fromSeq), mSourceLedger(ledger),
	    mIdent(ident), mInLedger(0), mStatus(NEW)
{
    assert(fromAccount.GetAddress()==fromLocalAccount.mAddress);
    assert(fromLocalAccount.mAmount>=amount);
    assert((fromSeq+1)==fromLocalAccount.mSeqNum);

    mAccountFrom=fromAccount.GetAddress();
    Sign(fromLocalAccount, fromAccount);
}

bool Transaction::Sign(LocalAccount &fromLocalAccount, const Account &fromAccount)
{
    if( (mAmount==0) || (mSourceLedger==0) || (mAccountTo==0) )
        return false;
    if((mAccountFrom!=fromLocalAccount.mAddress)||(mAccountFrom!=fromAccount.GetAddress()))
        return false;
    
    UpdateHash();

    std::vector<unsigned char> toSign, Signature;
    if(!GetRawUnsigned(toSign, fromAccount)) return false;
    if(!fromLocalAccount.SignRaw(toSign, Signature)) return false;
    mSignature=Signature;
    return true;
}

bool Transaction::CheckSign(const Account &fromAccount) const
{
    if(mAccountFrom!=fromAccount.GetAddress()) return false;

    std::vector<unsigned char> toSign;
    if(!GetRawUnsigned(toSign, fromAccount)) return false;

    return fromAccount.CheckSignRaw(toSign, mSignature);
}

bool Transaction::GetRawUnsigned(std::vector<unsigned char> &raw, const Account &fromAccount) const
{
    raw.clear();
        
}

#if 0
void Transaction::UpdateHash()
{ // FIXME
    vector<unsigned char> buffer;
    buffer.resize(trans->ByteSize());
    trans->SerializeToArray(&(buffer[0]),buffer.size());
    return Hash(buffer.begin(), buffer.end());
}
#endif
