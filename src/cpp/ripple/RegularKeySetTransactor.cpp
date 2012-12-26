#include "RegularKeySetTransactor.h"
#include "Log.h"


SETUP_LOG();


uint64_t RegularKeySetTransactor::calculateBaseFee()
{
	if ( !(mTxnAccount->getFlags() & lsfPasswordSpent) && 
		 (mSigningPubKey.getAccountID() == mTxnAccountID))
	{  // flag is armed and they signed with the right account
		return 0;
	}
	return Transactor::calculateBaseFee();
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
