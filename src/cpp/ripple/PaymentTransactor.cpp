#include "PaymentTransactor.h"
#include "Config.h"
#include "RippleCalc.h"
#include "Application.h"

#define RIPPLE_PATHS_MAX	3

SETUP_LOG();

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
	const bool		bXRPDirect		= uSrcCurrency.isZero() && uDstCurrency.isZero();

	cLog(lsINFO) << boost::str(boost::format("Payment> saMaxAmount=%s saDstAmount=%s")
		% saMaxAmount.getFullText()
		% saDstAmount.getFullText());

	if (uTxFlags & tfPaymentMask)
	{
		cLog(lsINFO) << "Payment: Malformed transaction: Invalid flags set.";

		return temINVALID_FLAG;
	}
	else if (!uDstAccountID)
	{
		cLog(lsINFO) << "Payment: Malformed transaction: Payment destination account not specified.";

		return temDST_NEEDED;
	}
	else if (bMax && !saMaxAmount.isPositive())
	{
		cLog(lsINFO) << "Payment: Malformed transaction: bad max amount: " << saMaxAmount.getFullText();

		return temBAD_AMOUNT;
	}
	else if (!saDstAmount.isPositive())
	{
		cLog(lsINFO) << "Payment: Malformed transaction: bad dst amount: " << saDstAmount.getFullText();

		return temBAD_AMOUNT;
	}
	else if (mTxnAccountID == uDstAccountID && uSrcCurrency == uDstCurrency && !bPaths)
	{
		cLog(lsINFO) << boost::str(boost::format("Payment: Malformed transaction: Redundant transaction: src=%s, dst=%s, src_cur=%s, dst_cur=%s")
			% mTxnAccountID.ToString()
			% uDstAccountID.ToString()
			% uSrcCurrency.ToString()
			% uDstCurrency.ToString());

		return temREDUNDANT;
	}
	else if (bMax && saMaxAmount == saDstAmount && saMaxAmount.getCurrency() == saDstAmount.getCurrency())
	{
		cLog(lsINFO) << "Payment: Malformed transaction: Redundant SendMax.";

		return temREDUNDANT_SEND_MAX;
	}
	else if (bXRPDirect && bMax)
	{
		cLog(lsINFO) << "Payment: Malformed transaction: SendMax specified for XRP to XRP.";

		return temBAD_SEND_XRP_MAX;
	}
	else if (bXRPDirect && bPaths)
	{
		cLog(lsINFO) << "Payment: Malformed transaction: Paths specified for XRP to XRP.";

		return temBAD_SEND_XRP_PATHS;
	}
	else if (bXRPDirect && bPartialPayment)
	{
		cLog(lsINFO) << "Payment: Malformed transaction: Partial payment specified for XRP to XRP.";

		return temBAD_SEND_XRP_PARTIAL;
	}
	else if (bXRPDirect && bLimitQuality)
	{
		cLog(lsINFO) << "Payment: Malformed transaction: Limit quality specified for XRP to XRP.";

		return temBAD_SEND_XRP_LIMIT;
	}
	else if (bXRPDirect && bNoRippleDirect)
	{
		cLog(lsINFO) << "Payment: Malformed transaction: No ripple direct specified for XRP to XRP.";

		return temBAD_SEND_XRP_NO_DIRECT;
	}

	SLE::pointer	sleDst	= mEngine->entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uDstAccountID));
	if (!sleDst)
	{
		// Destination account does not exist.

		if (!saDstAmount.isNative())
		{
			cLog(lsINFO) << "Payment: Delay transaction: Destination account does not exist.";

			// Another transaction could create the account and then this transaction would succeed.
			return tecNO_DST;
		}
		else if (isSetBit(mParams, tapOPEN_LEDGER) && bPartialPayment)
		{
			cLog(lsINFO) << "Payment: Delay transaction: Partial payment not allowed to create account.";
			// Make retry work smaller, by rejecting this.

			// Another transaction could create the account and then this transaction would succeed.
			return telNO_DST_PARTIAL;
		}
		else if (saDstAmount.getNValue() < mEngine->getLedger()->getReserve(0))	// Reserve is not scaled by load.
		{
			cLog(lsINFO) << "Payment: Delay transaction: Destination account does not exist. Insufficent payment to create account.";

			// Another transaction could create the account and then this transaction would succeed.
			return tecNO_DST_INSUF_XRP;
		}

		// Create the account.
		sleDst	= mEngine->entryCreate(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uDstAccountID));

		sleDst->setFieldAccount(sfAccount, uDstAccountID);
		sleDst->setFieldU32(sfSequence, 1);
	}
	else if ((sleDst->getFlags() & lsfRequireDestTag) && !mTxn.isFieldPresent(sfDestinationTag))
	{
		cLog(lsINFO) << "Payment: Malformed transaction: DestinationTag required.";

		return temDST_TAG_NEEDED;
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

		try
		{
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
		catch (const std::exception& e)
		{
			cLog(lsINFO) << "Payment: Caught throw: " << e.what();

			terResult	= tefEXCEPTION;
		}
	}
	else
	{
		// Direct XRP payment.

		const STAmount	saSrcXRPBalance	= mTxnAccount->getFieldAmount(sfBalance);
		const uint32	uOwnerCount		= mTxnAccount->getFieldU32(sfOwnerCount);
		const uint64	uReserve		= mEngine->getLedger()->getReserve(uOwnerCount);
		STAmount		saPaid			= mTxn.getTransactionFee();

		// Make sure have enough reserve to send. Allow final spend to use reserve for fee.
		if (saSrcXRPBalance + saPaid < saDstAmount + uReserve)		// Reserve is not scaled by fee.
		{
			// Vote no. However, transaction might succeed, if applied in a different order.
			cLog(lsINFO) << "";
			cLog(lsINFO) << boost::str(boost::format("Payment: Delay transaction: Insufficient funds: %s / %s (%d)")
				% saSrcXRPBalance.getText() % (saDstAmount + uReserve).getText() % uReserve);

			terResult	= tecUNFUNDED_PAYMENT;
		}
		else
		{
			mTxnAccount->setFieldAmount(sfBalance, saSrcXRPBalance - saDstAmount);
			sleDst->setFieldAmount(sfBalance, sleDst->getFieldAmount(sfBalance) + saDstAmount);

			// re-arm the password change fee if we can and need to
			if ((sleDst->getFlags() & lsfPasswordSpent))
				sleDst->clearFlag(lsfPasswordSpent);

			terResult	= tesSUCCESS;
		}
	}

	std::string	strToken;
	std::string	strHuman;

	if (transResultInfo(terResult, strToken, strHuman))
	{
		cLog(lsINFO) << boost::str(boost::format("Payment: %s: %s") % strToken % strHuman);
	}
	else
	{
		assert(false);
	}

	return terResult;
}

// vim:ts=4
