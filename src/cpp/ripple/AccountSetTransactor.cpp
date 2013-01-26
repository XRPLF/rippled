#include "AccountSetTransactor.h"
#include "Config.h"

SETUP_LOG();

TER AccountSetTransactor::doApply()
{
	cLog(lsINFO) << "AccountSet>";

	const uint32	uTxFlags	= mTxn.getFlags();

	const uint32	uFlagsIn	= mTxnAccount->getFieldU32(sfFlags);
	uint32			uFlagsOut	= uFlagsIn;

	if (uTxFlags & tfAccountSetMask)
	{
		cLog(lsINFO) << "AccountSet: Malformed transaction: Invalid flags set.";

		return temINVALID_FLAG;
	}

	//
	// RequireAuth
	//

	if ((tfRequireAuth|tfOptionalAuth) == (uTxFlags & (tfRequireAuth|tfOptionalAuth)))
	{
		cLog(lsINFO) << "AccountSet: Malformed transaction: Contradictory flags set.";

		return temINVALID_FLAG;
	}

	if ((uTxFlags & tfRequireAuth) && !isSetBit(uFlagsIn, lsfRequireAuth))
	{
		if (mTxn.getFieldU32(sfOwnerCount))
		{
			cLog(lsINFO) << "AccountSet: Retry: OwnerCount not zero.";

			return terOWNERS;
		}

		cLog(lsINFO) << "AccountSet: Set RequireAuth.";

		uFlagsOut	|= lsfRequireAuth;
	}

	if (uTxFlags & tfOptionalAuth)
	{
		cLog(lsINFO) << "AccountSet: Clear RequireAuth.";

		uFlagsOut	&= ~lsfRequireAuth;
	}

	//
	// RequireDestTag
	//

	if ((tfRequireDestTag|tfOptionalDestTag) == (uTxFlags & (tfRequireDestTag|tfOptionalDestTag)))
	{
		cLog(lsINFO) << "AccountSet: Malformed transaction: Contradictory flags set.";

		return temINVALID_FLAG;
	}

	if (uTxFlags & tfRequireDestTag)
	{
		cLog(lsINFO) << "AccountSet: Set RequireDestTag.";

		uFlagsOut	|= lsfRequireDestTag;
	}

	if (uTxFlags & tfOptionalDestTag)
	{
		cLog(lsINFO) << "AccountSet: Clear RequireDestTag.";

		uFlagsOut	&= ~lsfRequireDestTag;
	}

	if (uFlagsIn != uFlagsOut)
		mTxnAccount->setFieldU32(sfFlags, uFlagsOut);

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

	if (mTxn.isFieldPresent(sfMessageKey))
	{
		std::vector<unsigned char>	vucPublic	= mTxn.getFieldVL(sfMessageKey);

		if (vucPublic.size() > PUBLIC_BYTES_MAX)
		{
			cLog(lsINFO) << "AccountSet: message key too long";

			return telBAD_PUBLIC_KEY;
		}
		else
		{
			cLog(lsINFO) << "AccountSet: set message key";

			mTxnAccount->setFieldVL(sfMessageKey, vucPublic);
		}
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
		else if (vucDomain.size() > DOMAIN_BYTES_MAX)
		{
			cLog(lsINFO) << "AccountSet: domain too long";

			return telBAD_DOMAIN;
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
