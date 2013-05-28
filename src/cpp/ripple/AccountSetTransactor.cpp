SETUP_LOG (AccountSetTransactor)

TER AccountSetTransactor::doApply()
{
	WriteLog (lsINFO, AccountSetTransactor) << "AccountSet>";

	const uint32	uTxFlags	= mTxn.getFlags();

	const uint32	uFlagsIn	= mTxnAccount->getFieldU32(sfFlags);
	uint32			uFlagsOut	= uFlagsIn;

	if (uTxFlags & tfAccountSetMask)
	{
		WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: Malformed transaction: Invalid flags set.";

		return temINVALID_FLAG;
	}

	//
	// RequireAuth
	//

	if ((tfRequireAuth|tfOptionalAuth) == (uTxFlags & (tfRequireAuth|tfOptionalAuth)))
	{
		WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: Malformed transaction: Contradictory flags set.";

		return temINVALID_FLAG;
	}

	if ((uTxFlags & tfRequireAuth) && !isSetBit(uFlagsIn, lsfRequireAuth))
	{
		if (mTxnAccount->getFieldU32(sfOwnerCount))
		{
			WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: Retry: OwnerCount not zero.";

			return terOWNERS;
		}

		WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: Set RequireAuth.";

		uFlagsOut	|= lsfRequireAuth;
	}

	if ((uTxFlags & tfOptionalAuth) && isSetBit(uFlagsIn, lsfRequireAuth))
	{
		WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: Clear RequireAuth.";

		uFlagsOut	&= ~lsfRequireAuth;
	}

	//
	// RequireDestTag
	//

	if ((tfRequireDestTag|tfOptionalDestTag) == (uTxFlags & (tfRequireDestTag|tfOptionalDestTag)))
	{
		WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: Malformed transaction: Contradictory flags set.";

		return temINVALID_FLAG;
	}

	if ((uTxFlags & tfRequireDestTag) && !isSetBit(uFlagsIn, lsfRequireDestTag))
	{
		WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: Set lsfRequireDestTag.";

		uFlagsOut	|= lsfRequireDestTag;
	}

	if ((uTxFlags & tfOptionalDestTag) && isSetBit(uFlagsIn, lsfRequireDestTag))
	{
		WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: Clear lsfRequireDestTag.";

		uFlagsOut	&= ~lsfRequireDestTag;
	}

	//
	// DisallowXRP
	//

	if ((tfDisallowXRP|tfAllowXRP) == (uTxFlags & (tfDisallowXRP|tfAllowXRP)))
	{
		WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: Malformed transaction: Contradictory flags set.";

		return temINVALID_FLAG;
	}

	if ((uTxFlags & tfDisallowXRP) && !isSetBit(uFlagsIn, lsfDisallowXRP))
	{
		WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: Set lsfDisallowXRP.";

		uFlagsOut	|= lsfDisallowXRP;
	}

	if ((uTxFlags & tfAllowXRP) && isSetBit(uFlagsIn, lsfDisallowXRP))
	{
		WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: Clear lsfDisallowXRP.";

		uFlagsOut	&= ~lsfDisallowXRP;
	}

	//
	// EmailHash
	//

	if (mTxn.isFieldPresent(sfEmailHash))
	{
		uint128		uHash	= mTxn.getFieldH128(sfEmailHash);

		if (!uHash)
		{
			WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: unset email hash";

			mTxnAccount->makeFieldAbsent(sfEmailHash);
		}
		else
		{
			WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: set email hash";

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
			WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: unset wallet locator";

			mTxnAccount->makeFieldAbsent(sfEmailHash);
		}
		else
		{
			WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: set wallet locator";

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
			WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: message key too long";

			return telBAD_PUBLIC_KEY;
		}
		else
		{
			WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: set message key";

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
			WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: unset domain";

			mTxnAccount->makeFieldAbsent(sfDomain);
		}
		else if (vucDomain.size() > DOMAIN_BYTES_MAX)
		{
			WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: domain too long";

			return telBAD_DOMAIN;
		}
		else
		{
			WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: set domain";

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
			WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: unset transfer rate";

			mTxnAccount->makeFieldAbsent(sfTransferRate);
		}
		else if (uRate > QUALITY_ONE)
		{
			WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: set transfer rate";

			mTxnAccount->setFieldU32(sfTransferRate, uRate);
		}
		else
		{
			WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: bad transfer rate";

			return temBAD_TRANSFER_RATE;
		}
	}

	if (uFlagsIn != uFlagsOut)
		mTxnAccount->setFieldU32(sfFlags, uFlagsOut);

	WriteLog (lsINFO, AccountSetTransactor) << "AccountSet<";

	return tesSUCCESS;
}

// vim:ts=4
