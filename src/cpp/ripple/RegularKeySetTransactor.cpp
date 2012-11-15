#include "RegularKeySetTransactor.h"
#include "Log.h"


SETUP_LOG();

// TODO: 
TER RegularKeySetTransactor::checkSig()
{
	// Transaction's signing public key must be for the source account.
	// To prove the master private key made this transaction.
	if (mSigningPubKey.getAccountID() != mTxnAccountID)
	{
		// Signing Pub Key must be for Source Account ID.
		cLog(lsWARNING) << "sourceAccountID: " << mSigningPubKey.humanAccountID();
		cLog(lsWARNING) << "txn accountID: " << mTxn.getSourceAccount().humanAccountID();

		return temBAD_SET_ID;
	}
	return tesSUCCESS;
}

// TODO: this should be default fee if flag isn't set
void RegularKeySetTransactor::calculateFee()
{
	mFeeDue	= 0;
}


// TODO: change to take a fee if there is one there
TER RegularKeySetTransactor::doApply()
{
	std::cerr << "doRegularKeySet>" << std::endl;

	if (mTxnAccount->getFlags() & lsfPasswordSpent)
	{
		std::cerr << "doRegularKeySet: Delay transaction: Funds already spent." << std::endl;

		return terFUNDS_SPENT;
	}

	mTxnAccount->setFlag(lsfPasswordSpent);

	uint160	uAuthKeyID=mTxn.getFieldAccount160(sfAuthorizedKey);
	mTxnAccount->setFieldAccount(sfAuthorizedKey, uAuthKeyID);


	std::cerr << "doRegularKeySet<" << std::endl;

	return tesSUCCESS;
}
