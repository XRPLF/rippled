#include "OfferCancelTransactor.h"
#include "Log.h"

TER OfferCancelTransactor::doApply()
{
	TER				terResult;
	const uint32	uOfferSequence			= mTxn.getFieldU32(sfOfferSequence);
	const uint32	uAccountSequenceNext	= mTxnAccount->getFieldU32(sfSequence);

	Log(lsDEBUG) << "doOfferCancel: uAccountSequenceNext=" << uAccountSequenceNext << " uOfferSequence=" << uOfferSequence;

	if (!uOfferSequence || uAccountSequenceNext-1 <= uOfferSequence)
	{
		Log(lsINFO) << "doOfferCancel: uAccountSequenceNext=" << uAccountSequenceNext << " uOfferSequence=" << uOfferSequence;

		terResult	= temBAD_SEQUENCE;
	}
	else
	{
		const uint256	uOfferIndex	= Ledger::getOfferIndex(mTxnAccountID, uOfferSequence);
		SLE::pointer	sleOffer	= mEngine->entryCache(ltOFFER, uOfferIndex);

		if (sleOffer)
		{
			Log(lsWARNING) << "doOfferCancel: uOfferSequence=" << uOfferSequence;

			terResult	= mEngine->getNodes().offerDelete(sleOffer, uOfferIndex, mTxnAccountID);
		}
		else
		{
			Log(lsWARNING) << "doOfferCancel: offer not found: "
				<< RippleAddress::createHumanAccountID(mTxnAccountID)
				<< " : " << uOfferSequence
				<< " : " << uOfferIndex.ToString();

			terResult	= tesSUCCESS;
		}
	}

	return terResult;
}

// vim:ts=4
