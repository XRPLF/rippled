#include "RegularKeySetTransactor.h"
#include "Log.h"


SETUP_LOG();


void RegularKeySetTransactor::calculateFee()
{
	Transactor::calculateFee();

	if ( !(mTxnAccount->getFlags() & lsfPasswordSpent) && 
		 (mSigningPubKey.getAccountID() == mTxnAccountID))
	{  // flag is armed and they signed with the right account
		
		mSourceBalance	= mTxnAccount->getFieldAmount(sfBalance);
		if(mSourceBalance < mFeeDue) mFeeDue	= 0;
	}
}


TER RegularKeySetTransactor::doApply()
{
	std::cerr << "doRegularKeySet>" << std::endl;

	if(mFeeDue.isZero())
	{
		mTxnAccount->setFlag(lsfPasswordSpent);
	}

	uint160	uAuthKeyID=mTxn.getFieldAccount160(sfRegularKey);
	mTxnAccount->setFieldAccount(sfRegularKey, uAuthKeyID);


	std::cerr << "doRegularKeySet<" << std::endl;

	return tesSUCCESS;
}
