#include "TrustSetTransactor.h"

#include <boost/bind.hpp>

TER TrustSetTransactor::doApply()
{
	TER			terResult		= tesSUCCESS;
	Log(lsINFO) << "doTrustSet>";

	const STAmount		saLimitAmount	= mTxn.getFieldAmount(sfLimitAmount);
	const bool			bQualityIn		= mTxn.isFieldPresent(sfQualityIn);
	const bool			bQualityOut		= mTxn.isFieldPresent(sfQualityOut);
	const uint160		uCurrencyID		= saLimitAmount.getCurrency();
	uint160				uDstAccountID	= saLimitAmount.getIssuer();
	const bool			bFlipped		= mTxnAccountID > uDstAccountID;		// true, iff current is not lowest.

	uint32				uQualityIn		= bQualityIn ? mTxn.getFieldU32(sfQualityIn) : 0;
	uint32				uQualityOut		= bQualityIn ? mTxn.getFieldU32(sfQualityOut) : 0;

	if (bQualityIn && QUALITY_ONE == uQualityIn)
		uQualityIn	= 0;

	if (bQualityOut && QUALITY_ONE == uQualityOut)
		uQualityOut	= 0;

	// Check if destination makes sense.

	if (saLimitAmount.isNegative())
	{
		Log(lsINFO) << "doTrustSet: Malformed transaction: Negatived credit limit.";

		return temBAD_AMOUNT;
	}
	else if (!uDstAccountID || uDstAccountID == ACCOUNT_ONE)
	{
		Log(lsINFO) << "doTrustSet: Malformed transaction: Destination account not specified.";

		return temDST_NEEDED;
	}
	else if (mTxnAccountID == uDstAccountID)
	{
		Log(lsINFO) << "doTrustSet: Malformed transaction: Can not extend credit to self.";

		return temDST_IS_SRC;
	}

	SLE::pointer		sleDst			= mEngine->entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uDstAccountID));
	if (!sleDst)
	{
		Log(lsINFO) << "doTrustSet: Delay transaction: Destination account does not exist.";

		return terNO_DST;
	}

	STAmount			saLimitAllow	= saLimitAmount;
	saLimitAllow.setIssuer(mTxnAccountID);

	SLE::pointer		sleRippleState	= mEngine->entryCache(ltRIPPLE_STATE, Ledger::getRippleStateIndex(mTxnAccountID, uDstAccountID, uCurrencyID));
	if (sleRippleState)
	{
		STAmount		saLowBalance;
		STAmount		saLowLimit;
		STAmount		saHighBalance;
		STAmount		saHighLimit;
		uint32			uLowQualityIn;
		uint32			uLowQualityOut;
		uint32			uHighQualityIn;
		uint32			uHighQualityOut;
		const uint160&	uLowAccountID	= !bFlipped ? mTxnAccountID : uDstAccountID;
		const uint160&	uHighAccountID	=  bFlipped ? mTxnAccountID : uDstAccountID;
		SLE::ref		sleLowAccount	= !bFlipped ? mTxnAccount : sleDst;
		SLE::ref		sleHighAccount	=  bFlipped ? mTxnAccount : sleDst;

		//
		// Balances
		//

		saLowBalance	= sleRippleState->getFieldAmount(sfBalance);
		saHighBalance	= saLowBalance;

		if (bFlipped)
		{
			saLowBalance.negate();
		}
		else
		{
			saHighBalance.negate();
		}

		//
		// Limits
		//

		if (bFlipped)
		{
			sleRippleState->setFieldAmount(sfHighLimit, saLimitAllow);

			saLowLimit	= sleRippleState->getFieldAmount(sfLowLimit);
			saHighLimit	= saLimitAllow;
		}
		else
		{
			sleRippleState->setFieldAmount(sfLowLimit, saLimitAllow);

			saLowLimit	= saLimitAllow;
			saHighLimit	= sleRippleState->getFieldAmount(sfHighLimit);
		}

		//
		// Quality in
		//

		if (!bQualityIn)
		{
			// Not setting. Just get it.

			uLowQualityIn	= sleRippleState->getFieldU32(sfLowQualityIn);
			uHighQualityIn	= sleRippleState->getFieldU32(sfHighQualityIn);
		}
		else if (uQualityIn)
		{
			// Setting.

			sleRippleState->setFieldU32(!bFlipped ? sfLowQualityIn : sfHighQualityIn, uQualityIn);

			uLowQualityIn	= !bFlipped ? uQualityIn : sleRippleState->getFieldU32(sfLowQualityIn);
			uHighQualityIn	=  bFlipped ? uQualityIn : sleRippleState->getFieldU32(sfHighQualityIn);
		}
		else
		{
			// Clearing.

			sleRippleState->makeFieldAbsent(!bFlipped ? sfLowQualityIn : sfHighQualityIn);

			uLowQualityIn	= !bFlipped ? 0 : sleRippleState->getFieldU32(sfLowQualityIn);
			uHighQualityIn	=  bFlipped ? 0 : sleRippleState->getFieldU32(sfHighQualityIn);
		}

		//
		// Quality out
		//

		if (!bQualityOut)
		{
			// Not setting. Just get it.

			uLowQualityOut	= sleRippleState->getFieldU32(sfLowQualityOut);
			uHighQualityOut	= sleRippleState->getFieldU32(sfHighQualityOut);
		}
		else if (uQualityOut)
		{
			// Setting.

			sleRippleState->setFieldU32(!bFlipped ? sfLowQualityOut : sfHighQualityOut, uQualityOut);

			uLowQualityOut	= !bFlipped ? uQualityOut : sleRippleState->getFieldU32(sfLowQualityOut);
			uHighQualityOut	=  bFlipped ? uQualityOut : sleRippleState->getFieldU32(sfHighQualityOut);
		}
		else
		{
			// Clearing.

			sleRippleState->makeFieldAbsent(!bFlipped ? sfLowQualityOut : sfHighQualityOut);

			uLowQualityOut	= !bFlipped ? 0 : sleRippleState->getFieldU32(sfLowQualityOut);
			uHighQualityOut	=  bFlipped ? 0 : sleRippleState->getFieldU32(sfHighQualityOut);
		}

		if (QUALITY_ONE == uLowQualityIn)	uLowQualityIn	= 0;
		if (QUALITY_ONE == uHighQualityIn)	uHighQualityIn	= 0;
		if (QUALITY_ONE == uLowQualityOut)	uLowQualityOut	= 0;
		if (QUALITY_ONE == uHighQualityOut)	uHighQualityOut	= 0;

		const bool	bLowReserveSet		= uLowQualityIn || uLowQualityOut || !!saLowLimit || saLowBalance.isPositive();
		const bool	bLowReserveClear	= !uLowQualityIn && !uLowQualityOut && !saLowLimit && !saLowBalance.isPositive();

		const bool	bHighReserveSet		= uHighQualityIn || uHighQualityOut || !!saHighLimit || saHighBalance.isPositive();
		const bool	bHighReserveClear	= !uHighQualityIn && !uHighQualityOut && !saHighLimit && !saHighBalance.isPositive();
		const bool	bDefault			= bLowReserveClear && bHighReserveClear;

		const uint32	uFlagsIn		= sleRippleState->getFieldU32(sfFlags);
		uint32			uFlagsOut		= uFlagsIn;

		const bool	bLowReserved		= isSetBit(uFlagsIn, lsfLowReserve);
		const bool	bHighReserved		= isSetBit(uFlagsIn, lsfHighReserve);

		if (bLowReserveSet && !bLowReserved)
		{
			// Set reserve for low account.

			terResult	= mEngine->getNodes().ownerCountAdjust(uLowAccountID, 1, sleLowAccount);
			uFlagsOut	|= lsfLowReserve;
		}

		if (bLowReserveClear && bLowReserved)
		{
			// Clear reserve for low account.

			terResult	= mEngine->getNodes().ownerCountAdjust(uLowAccountID, -1, sleLowAccount);
			uFlagsOut	&= ~lsfLowReserve;
		}

		if (bHighReserveSet && !bHighReserved)
		{
			// Set reserve for high account.

			terResult	= mEngine->getNodes().ownerCountAdjust(uHighAccountID, 1, sleHighAccount);
			uFlagsOut	|= lsfHighReserve;
		}

		if (bHighReserveClear && bHighReserved)
		{
			// Clear reserve for high account.

			terResult	= mEngine->getNodes().ownerCountAdjust(uHighAccountID, -1, sleHighAccount);
			uFlagsOut	&= ~lsfHighReserve;
		}

		if (uFlagsIn != uFlagsOut)
			sleRippleState->setFieldU32(sfFlags, uFlagsOut);

		if (bDefault)
		{
			// Can delete.

			uint64		uSrcRef;							// <-- Ignored, dirs never delete.

			terResult	= mEngine->getNodes().dirDelete(false, uSrcRef, Ledger::getOwnerDirIndex(uLowAccountID), sleRippleState->getIndex(), false);

			if (tesSUCCESS == terResult)
				terResult	= mEngine->getNodes().dirDelete(false, uSrcRef, Ledger::getOwnerDirIndex(uHighAccountID), sleRippleState->getIndex(), false);

			mEngine->entryDelete(sleRippleState);

			Log(lsINFO) << "doTrustSet: Deleting ripple line";
		}
		else
		{
			mEngine->entryModify(sleRippleState);

			Log(lsINFO) << "doTrustSet: Modify ripple line";
		}
	}
	// Line does not exist.
	else if (!saLimitAmount									// Setting default limit.
		&& bQualityIn && !uQualityIn						// Setting default quality in.
		&& bQualityOut && !uQualityOut						// Setting default quality out.
		)
	{
		Log(lsINFO) << "doTrustSet: Redundant: Setting non-existent ripple line to defaults.";

		return terNO_LINE_REDUNDANT;
	}
	else
	{
		// Create a new ripple line.
		sleRippleState	= mEngine->entryCreate(ltRIPPLE_STATE, Ledger::getRippleStateIndex(mTxnAccountID, uDstAccountID, uCurrencyID));

		Log(lsINFO) << "doTrustSet: Creating ripple line: " << sleRippleState->getIndex().ToString();

		sleRippleState->setFieldAmount(sfBalance, STAmount(uCurrencyID, ACCOUNT_ONE));	// Zero balance in currency.
		sleRippleState->setFieldAmount(!bFlipped ? sfLowLimit : sfHighLimit, saLimitAllow);
		sleRippleState->setFieldAmount( bFlipped ? sfLowLimit : sfHighLimit, STAmount(uCurrencyID, uDstAccountID));

		if (uQualityIn)
			sleRippleState->setFieldU32(!bFlipped ? sfLowQualityIn : sfHighQualityIn, uQualityIn);

		if (uQualityOut)
			sleRippleState->setFieldU32(!bFlipped ? sfLowQualityOut : sfHighQualityOut, uQualityOut);

		sleRippleState->setFieldU32(sfFlags, !bFlipped ? lsfLowReserve : lsfHighReserve);

		uint64			uSrcRef;							// <-- Ignored, dirs never delete.

		terResult	= mEngine->getNodes().dirAdd(
			uSrcRef,
			Ledger::getOwnerDirIndex(mTxnAccountID),
			sleRippleState->getIndex(),
			boost::bind(&Ledger::ownerDirDescriber, _1, mTxnAccountID));

		if (tesSUCCESS == terResult)
			terResult	= mEngine->getNodes().ownerCountAdjust(mTxnAccountID, 1, mTxnAccount);

		if (tesSUCCESS == terResult)
			terResult	= mEngine->getNodes().dirAdd(
				uSrcRef,
				Ledger::getOwnerDirIndex(uDstAccountID),
				sleRippleState->getIndex(),
				boost::bind(&Ledger::ownerDirDescriber, _1, uDstAccountID));
	}

	Log(lsINFO) << "doTrustSet<";

	return terResult;
}

// vim:ts=4
