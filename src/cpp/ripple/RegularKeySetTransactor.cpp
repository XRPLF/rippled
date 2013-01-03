#include "RegularKeySetTransactor.h"
#include "Log.h"

SETUP_LOG();

uint64_t RegularKeySetTransactor::calculateBaseFee()
{
	if ( !(mTxnAccount->getFlags() & lsfPasswordSpent)
		&& (mSigningPubKey.getAccountID() == mTxnAccountID))
	{  // flag is armed and they signed with the right account
		return 0;
	}
	return Transactor::calculateBaseFee();
}


TER RegularKeySetTransactor::doApply()
{
	std::cerr << "RegularKeySet>" << std::endl;

	const uint32		uTxFlags		= mTxn.getFlags();

	if (uTxFlags)
	{
		cLog(lsINFO) << "RegularKeySet: Malformed transaction: Invalid flags set.";

		return temINVALID_FLAG;
	}

	if (mFeeDue.isZero())
	{
		mTxnAccount->setFlag(lsfPasswordSpent);
	}

	uint160	uAuthKeyID=mTxn.getFieldAccount160(sfRegularKey);
	mTxnAccount->setFieldAccount(sfRegularKey, uAuthKeyID);

	std::cerr << "RegularKeySet<" << std::endl;

	return tesSUCCESS;
}

// vim:ts=4
