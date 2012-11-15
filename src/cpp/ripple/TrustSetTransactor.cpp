#include "TrustSetTransactor.h"

TER TrustSetTransactor::doApply()
{
	TER			terResult		= tesSUCCESS;
	Log(lsINFO) << "doTrustSet>";

	const STAmount		saLimitAmount	= mTxn.getFieldAmount(sfLimitAmount);
	const bool			bQualityIn		= mTxn.isFieldPresent(sfQualityIn);
	const uint32		uQualityIn		= bQualityIn ? mTxn.getFieldU32(sfQualityIn) : 0;
	const bool			bQualityOut		= mTxn.isFieldPresent(sfQualityOut);
	const uint32		uQualityOut		= bQualityIn ? mTxn.getFieldU32(sfQualityOut) : 0;
	const uint160		uCurrencyID		= saLimitAmount.getCurrency();
	uint160				uDstAccountID	= saLimitAmount.getIssuer();
	const bool			bFlipped		= mTxnAccountID > uDstAccountID;		// true, iff current is not lowest.
	bool				bDelIndex		= false;

	// Check if destination makes sense.

	if (saLimitAmount.isNegative())
	{
		Log(lsINFO) << "doTrustSet: Malformed transaction: Negatived credit limit.";

		return temBAD_AMOUNT;
	}
	else if (!uDstAccountID)
	{
		Log(lsINFO) << "doTrustSet: Malformed transaction: Destination account not specified.";

		return temDST_NEEDED;
	}
	else if (mTxnAccountID == uDstAccountID)
	{
		Log(lsINFO) << "doTrustSet: Malformed transaction: Can not extend credit to self.";

		return temDST_IS_SRC;
	}

	SLE::pointer		sleDst		= mEngine->entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uDstAccountID));
	if (!sleDst)
	{
		Log(lsINFO) << "doTrustSet: Delay transaction: Destination account does not exist.";

		return terNO_DST;
	}

	STAmount		saLimitAllow	= saLimitAmount;
	saLimitAllow.setIssuer(mTxnAccountID);

	SLE::pointer		sleRippleState	= mEngine->entryCache(ltRIPPLE_STATE, Ledger::getRippleStateIndex(mTxnAccountID, uDstAccountID, uCurrencyID));
	if (sleRippleState)
	{
		// A line exists in one or more directions.
#if 0
		if (!saLimitAmount)
		{
			// Zeroing line.
			uint160		uLowID			= sleRippleState->getFieldAmount(sfLowLimit).getIssuer();
			uint160		uHighID			= sleRippleState->getFieldAmount(sfHighLimit).getIssuer();
			bool		bLow			= uLowID == uSrcAccountID;
			bool		bHigh			= uLowID == uDstAccountID;
			bool		bBalanceZero	= !sleRippleState->getFieldAmount(sfBalance);
			STAmount	saDstLimit		= sleRippleState->getFieldAmount(bSendLow ? sfLowLimit : sfHighLimit);
			bool		bDstLimitZero	= !saDstLimit;

			assert(bLow || bHigh);

			if (bBalanceZero && bDstLimitZero)
			{
				// Zero balance and eliminating last limit.

				bDelIndex	= true;
				terResult	= dirDelete(false, uSrcRef, Ledger::getOwnerDirIndex(mTxnAccountID), sleRippleState->getIndex(), false);
			}
		}
#endif

		if (!bDelIndex)
		{
			sleRippleState->setFieldAmount(bFlipped ? sfHighLimit: sfLowLimit, saLimitAllow);

			if (!bQualityIn)
			{
				nothing();
			}
			else if (uQualityIn)
			{
				sleRippleState->setFieldU32(bFlipped ? sfLowQualityIn : sfHighQualityIn, uQualityIn);
			}
			else
			{
				sleRippleState->makeFieldAbsent(bFlipped ? sfLowQualityIn : sfHighQualityIn);
			}

			if (!bQualityOut)
			{
				nothing();
			}
			else if (uQualityOut)
			{
				sleRippleState->setFieldU32(bFlipped ? sfLowQualityOut : sfHighQualityOut, uQualityOut);
			}
			else
			{
				sleRippleState->makeFieldAbsent(bFlipped ? sfLowQualityOut : sfHighQualityOut);
			}

			mEngine->entryModify(sleRippleState);
		}

		Log(lsINFO) << "doTrustSet: Modifying ripple line: bDelIndex=" << bDelIndex;
	}
	// Line does not exist.
	else if (!saLimitAmount)
	{
		Log(lsINFO) << "doTrustSet: Redundant: Setting non-existent ripple line to 0.";

		return terNO_LINE_NO_ZERO;
	}
	else
	{
		// Create a new ripple line.
		sleRippleState	= mEngine->entryCreate(ltRIPPLE_STATE, Ledger::getRippleStateIndex(mTxnAccountID, uDstAccountID, uCurrencyID));

		Log(lsINFO) << "doTrustSet: Creating ripple line: " << sleRippleState->getIndex().ToString();

		sleRippleState->setFieldAmount(sfBalance, STAmount(uCurrencyID, ACCOUNT_ONE));	// Zero balance in currency.
		sleRippleState->setFieldAmount(bFlipped ? sfHighLimit : sfLowLimit, saLimitAllow);
		sleRippleState->setFieldAmount(bFlipped ? sfLowLimit : sfHighLimit, STAmount(uCurrencyID, uDstAccountID));

		if (uQualityIn)
			sleRippleState->setFieldU32(bFlipped ? sfHighQualityIn : sfLowQualityIn, uQualityIn);
		if (uQualityOut)
			sleRippleState->setFieldU32(bFlipped ? sfHighQualityOut : sfLowQualityOut, uQualityOut);

		uint64			uSrcRef;							// Ignored, dirs never delete.

		terResult	= mEngine->getNodes().dirAdd(uSrcRef, Ledger::getOwnerDirIndex(mTxnAccountID), sleRippleState->getIndex());

		if (tesSUCCESS == terResult)
			terResult	= mEngine->getNodes().dirAdd(uSrcRef, Ledger::getOwnerDirIndex(uDstAccountID), sleRippleState->getIndex());
	}

	Log(lsINFO) << "doTrustSet<";

	return terResult;
}