#include "AccountSetTransactor.h"

SETUP_LOG();

TER AccountSetTransactor::doApply()
{
	cLog(lsINFO) << "doAccountSet>";

	//
	// EmailHash
	//

	if (mTxn.isFieldPresent(sfEmailHash))
	{
		uint128		uHash	= mTxn.getFieldH128(sfEmailHash);

		if (!uHash)
		{
			cLog(lsINFO) << "doAccountSet: unset email hash";

			mTxnAccount->makeFieldAbsent(sfEmailHash);
		}
		else
		{
			cLog(lsINFO) << "doAccountSet: set email hash";

			mTxnAccount->setFieldH128(sfEmailHash, uHash);
		}
	}

	//
	// WalletLocator
	//

	if (mTxn.isFieldPresent(sfWalletLocator))
	{
		uint256		uHash	= mTxn.getFieldH256(sfWalletLocator);

		if (!uHash)
		{
			cLog(lsINFO) << "doAccountSet: unset wallet locator";

			mTxnAccount->makeFieldAbsent(sfEmailHash);
		}
		else
		{
			cLog(lsINFO) << "doAccountSet: set wallet locator";

			mTxnAccount->setFieldH256(sfWalletLocator, uHash);
		}
	}

	//
	// MessageKey
	//

	if (!mTxn.isFieldPresent(sfMessageKey))
	{
		nothing();
	}
	else
	{
		cLog(lsINFO) << "doAccountSet: set message key";

		mTxnAccount->setFieldVL(sfMessageKey, mTxn.getFieldVL(sfMessageKey));
	}

	//
	// Domain
	//

	if (mTxn.isFieldPresent(sfDomain))
	{
		std::vector<unsigned char>	vucDomain	= mTxn.getFieldVL(sfDomain);

		if (vucDomain.empty())
		{
			cLog(lsINFO) << "doAccountSet: unset domain";

			mTxnAccount->makeFieldAbsent(sfDomain);
		}
		else
		{
			cLog(lsINFO) << "doAccountSet: set domain";

			mTxnAccount->setFieldVL(sfDomain, vucDomain);
		}
	}

	//
	// TransferRate
	//

	if (mTxn.isFieldPresent(sfTransferRate))
	{
		uint32		uRate	= mTxn.getFieldU32(sfTransferRate);

		if (!uRate || uRate == QUALITY_ONE)
		{
			cLog(lsINFO) << "doAccountSet: unset transfer rate";

			mTxnAccount->makeFieldAbsent(sfTransferRate);
		}
		else if (uRate > QUALITY_ONE)
		{
			cLog(lsINFO) << "doAccountSet: set transfer rate";

			mTxnAccount->setFieldU32(sfTransferRate, uRate);
		}
		else
		{
			cLog(lsINFO) << "doAccountSet: bad transfer rate";

			return temBAD_TRANSFER_RATE;
		}
	}

	cLog(lsINFO) << "doAccountSet<";

	return tesSUCCESS;
}
