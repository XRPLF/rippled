#include "PaymentTransactor.h"
#include "Config.h"
#include "RippleCalc.h"

#define RIPPLE_PATHS_MAX	3

TER PaymentTransactor::doApply()
{
	// Ripple if source or destination is non-native or if there are paths.
	const uint32	uTxFlags		= mTxn.getFlags();
	const bool		bPartialPayment	= isSetBit(uTxFlags, tfPartialPayment);
	const bool		bLimitQuality	= isSetBit(uTxFlags, tfLimitQuality);
	const bool		bNoRippleDirect	= isSetBit(uTxFlags, tfNoRippleDirect);
	const bool		bPaths			= mTxn.isFieldPresent(sfPaths);
	const bool		bMax			= mTxn.isFieldPresent(sfSendMax);
	const uint160	uDstAccountID	= mTxn.getFieldAccount160(sfDestination);
	const STAmount	saDstAmount		= mTxn.getFieldAmount(sfAmount);
	const STAmount	saMaxAmount		= bMax
		? mTxn.getFieldAmount(sfSendMax)
		: saDstAmount.isNative()
		? saDstAmount
		: STAmount(saDstAmount.getCurrency(), mTxnAccountID, saDstAmount.getMantissa(), saDstAmount.getExponent(), saDstAmount.isNegative());
	const uint160	uSrcCurrency	= saMaxAmount.getCurrency();
	const uint160	uDstCurrency	= saDstAmount.getCurrency();

	Log(lsINFO) << boost::str(boost::format("doPayment> saMaxAmount=%s saDstAmount=%s")
		% saMaxAmount.getFullText()
		% saDstAmount.getFullText());

	if (uTxFlags & tfPaymentMask)
	{
		Log(lsINFO) << "doPayment: Malformed transaction: Invalid flags set.";

		return temINVALID_FLAG;
	}
	else if (!uDstAccountID)
	{
		Log(lsINFO) << "doPayment: Malformed transaction: Payment destination account not specified.";

		return temDST_NEEDED;
	}
	else if (bMax && !saMaxAmount.isPositive())
	{
		Log(lsINFO) << "doPayment: Malformed transaction: bad max amount: " << saMaxAmount.getFullText();

		return temBAD_AMOUNT;
	}
	else if (!saDstAmount.isPositive())
	{
		Log(lsINFO) << "doPayment: Malformed transaction: bad dst amount: " << saDstAmount.getFullText();

		return temBAD_AMOUNT;
	}
	else if (mTxnAccountID == uDstAccountID && uSrcCurrency == uDstCurrency && !bPaths)
	{
		Log(lsINFO) << boost::str(boost::format("doPayment: Malformed transaction: Redundant transaction: src=%s, dst=%s, src_cur=%s, dst_cur=%s")
			% mTxnAccountID.ToString()
			% uDstAccountID.ToString()
			% uSrcCurrency.ToString()
			% uDstCurrency.ToString());

		return temREDUNDANT;
	}
	else if (bMax
		&& ((saMaxAmount == saDstAmount && saMaxAmount.getCurrency() == saDstAmount.getCurrency())
		|| (saDstAmount.isNative() && saMaxAmount.isNative())))
	{
		Log(lsINFO) << "doPayment: Malformed transaction: bad SendMax.";

		return temINVALID;
	}

	SLE::pointer	sleDst	= mEngine->entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uDstAccountID));
	if (!sleDst)
	{
		// Destination account does not exist.

		if (!saDstAmount.isNative())
		{
			// This restriction could be relaxed.
			Log(lsINFO) << "doPayment: Malformed transaction: Create account may only fund XRP.";

			return temCREATEXRP;
		}
		else if (isSetBit(mParams, tapOPEN_LEDGER)							// Ledger is not final, we can vote.
			&& saDstAmount.getNValue() < theConfig.FEE_ACCOUNT_RESERVE)		// Reserve is not scaled by fee.
		{
			Log(lsINFO) << "doPayment: Delay transaction: Destination account does not exist insufficent payment to create account.";

			// Not a local failure. Another transaction could create account and then this transaction would succeed.
			return terNO_DST;
		}

		// Create the account.
		sleDst	= mEngine->entryCreate(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uDstAccountID));

		sleDst->setFieldAccount(sfAccount, uDstAccountID);
		sleDst->setFieldU32(sfSequence, 1);
	}
	else
	{
		mEngine->entryModify(sleDst);
	}

	TER			terResult;
	// XXX Should bMax be sufficient to imply ripple?
	const bool	bRipple	= bPaths || bMax || !saDstAmount.isNative();

	if (bRipple)
	{
		// Ripple payment

		STPathSet	spsPaths = mTxn.getFieldPathSet(sfPaths);
		std::vector<PathState::pointer>	vpsExpanded;
		STAmount	saMaxAmountAct;
		STAmount	saDstAmountAct;

		terResult	= isSetBit(mParams, tapOPEN_LEDGER) && spsPaths.size() > RIPPLE_PATHS_MAX
			? telBAD_PATH_COUNT			// Too many paths for proposed ledger.
			: RippleCalc::rippleCalc(
				mEngine->getNodes(),
				saMaxAmountAct,
				saDstAmountAct,
				vpsExpanded,
				saMaxAmount,
				saDstAmount,
				uDstAccountID,
				mTxnAccountID,
				spsPaths,
				bPartialPayment,
				bLimitQuality,
				bNoRippleDirect,		// Always compute for finalizing ledger.
				false);					// Not standalone, delete unfundeds.
	}
	else
	{
		// Direct XRP payment.

		const STAmount	saSrcXRPBalance	= mTxnAccount->getFieldAmount(sfBalance);
		const uint32	uOwnerCount		= mTxn.getFieldU32(sfOwnerCount);
		const uint64	uReserve		= theConfig.FEE_ACCOUNT_RESERVE+uOwnerCount*theConfig.FEE_OWNER_RESERVE;

		// Make sure have enough reserve to send.
		if (isSetBit(mParams, tapOPEN_LEDGER)					// Ledger is not final, we can vote.
			&& saSrcXRPBalance < saDstAmount + uReserve)		// Reserve is not scaled by fee.
		{
			// Vote no. However, transaction might succeed, if applied in a different order.
			Log(lsINFO) << "doPayment: Delay transaction: Insufficient funds.";

			terResult	= terUNFUNDED;
		}
		else
		{
			mTxnAccount->setFieldAmount(sfBalance, saSrcXRPBalance - saDstAmount);

			// re-arm the password change fee if we can and need to
			if ( (sleDst->getFlags() & lsfPasswordSpent) &&
				 (saDstAmount > theConfig.FEE_DEFAULT) )
			{
				sleDst->setFieldAmount(sfBalance, sleDst->getFieldAmount(sfBalance) + saDstAmount-theConfig.FEE_DEFAULT);
				sleDst->clearFlag(lsfPasswordSpent);
			}
			else
			{
				sleDst->setFieldAmount(sfBalance, sleDst->getFieldAmount(sfBalance) + saDstAmount);
			}

			terResult	= tesSUCCESS;
		}
	}

	std::string	strToken;
	std::string	strHuman;

	if (transResultInfo(terResult, strToken, strHuman))
	{
		Log(lsINFO) << boost::str(boost::format("doPayment: %s: %s") % strToken % strHuman);
	}
	else
	{
		assert(false);
	}

	return terResult;
}

// vim:ts=4
