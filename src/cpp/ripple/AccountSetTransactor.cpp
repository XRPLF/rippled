#include "AccountSetTransactor.h"

SETUP_LOG();

TER AccountSetTransactor::doApply()
{
	cLog(lsINFO) << "AccountSet>";

	const uint32	uTxFlags	= mTxn.getFlags();

	if (uTxFlags)
	{
		cLog(lsINFO) << "AccountSet: Malformed transaction: Invalid flags set.";

		return temINVALID_FLAG;
	}

	//
	// EmailHash
	//

	if (mTxn.isFieldPresent(sfEmailHash))
	{
		uint128		uHash	= mTxn.getFieldH128(sfEmailHash);

		if (!uHash)
		{
			cLog(lsINFO) << "AccountSet: unset email hash";

			mTxnAccount->makeFieldAbsent(sfEmailHash);
		}
		else
		{
			cLog(lsINFO) << "AccountSet: set email hash";

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
			cLog(lsINFO) << "AccountSet: unset wallet locator";

			mTxnAccount->makeFieldAbsent(sfEmailHash);
		}
		else
		{
			cLog(lsINFO) << "AccountSet: set wallet locator";

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
		cLog(lsINFO) << "AccountSet: set message key";

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
			cLog(lsINFO) << "AccountSet: unset domain";

			mTxnAccount->makeFieldAbsent(sfDomain);
		}
		else
		{
			cLog(lsINFO) << "AccountSet: set domain";

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
			cLog(lsINFO) << "AccountSet: unset transfer rate";

			mTxnAccount->makeFieldAbsent(sfTransferRate);
		}
		else if (uRate > QUALITY_ONE)
		{
			cLog(lsINFO) << "AccountSet: set transfer rate";

			mTxnAccount->setFieldU32(sfTransferRate, uRate);
		}
		else
		{
			cLog(lsINFO) << "AccountSet: bad transfer rate";

			return temBAD_TRANSFER_RATE;
		}
	}

	cLog(lsINFO) << "AccountSet<";

	return tesSUCCESS;
}

// vim:ts=4
