#include "OfferCancelTransactor.h"
#include "Log.h"

SETUP_LOG();

TER OfferCancelTransactor::doApply()
{
	TER				terResult;
	const uint32	uOfferSequence			= mTxn.getFieldU32(sfOfferSequence);
	const uint32	uAccountSequenceNext	= mTxnAccount->getFieldU32(sfSequence);

	cLog(lsDEBUG) << "OfferCancel: uAccountSequenceNext=" << uAccountSequenceNext << " uOfferSequence=" << uOfferSequence;

	const uint32	uTxFlags				= mTxn.getFlags();

	if (uTxFlags)
	{
		cLog(lsINFO) << "OfferCancel: Malformed transaction: Invalid flags set.";

		return temINVALID_FLAG;
	}

	if (!uOfferSequence || uAccountSequenceNext-1 <= uOfferSequence)
	{
		cLog(lsINFO) << "OfferCancel: uAccountSequenceNext=" << uAccountSequenceNext << " uOfferSequence=" << uOfferSequence;

		terResult	= temBAD_SEQUENCE;
	}
	else
	{
		const uint256	uOfferIndex	= Ledger::getOfferIndex(mTxnAccountID, uOfferSequence);
		SLE::pointer	sleOffer	= mEngine->entryCache(ltOFFER, uOfferIndex);

		if (sleOffer)
		{
			cLog(lsWARNING) << "OfferCancel: uOfferSequence=" << uOfferSequence;

			terResult	= mEngine->getNodes().offerDelete(sleOffer, uOfferIndex, mTxnAccountID);
		}
		else
		{
			cLog(lsWARNING) << "OfferCancel: offer not found: "
				<< RippleAddress::createHumanAccountID(mTxnAccountID)
				<< " : " << uOfferSequence
				<< " : " << uOfferIndex.ToString();

			terResult	= tesSUCCESS;
		}
	}

	return terResult;
}

// vim:ts=4
