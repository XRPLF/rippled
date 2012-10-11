
#include <boost/foreach.hpp>
#include <boost/format.hpp>
#include <boost/tuple/tuple_comparison.hpp>

#include "RippleCalc.h"
#include "Log.h"

#include "../json/writer.h"

SETUP_LOG();

std::size_t hash_value(const aciSource& asValue)
{
	std::size_t seed = 0;

	asValue.get<0>().hash_combine(seed);
	asValue.get<1>().hash_combine(seed);
	asValue.get<2>().hash_combine(seed);

	return seed;
}

// If needed, advance to next funded offer.
// - Automatically advances to first offer.
// - Set bEntryAdvance to advance to next entry.
// <-- uOfferIndex : 0=end of list.
TER RippleCalc::calcNodeAdvance(
	const unsigned int			uIndex,				// 0 < uIndex < uLast
	const PathState::pointer&	pspCur,
	const bool					bMultiQuality,
	const bool					bReverse)
{
	PaymentNode&	pnPrv			= pspCur->vpnNodes[uIndex-1];
	PaymentNode&	pnCur			= pspCur->vpnNodes[uIndex];

	const uint160&	uPrvCurrencyID	= pnPrv.uCurrencyID;
	const uint160&	uPrvIssuerID	= pnPrv.uIssuerID;
	const uint160&	uCurCurrencyID	= pnCur.uCurrencyID;
	const uint160&	uCurIssuerID	= pnCur.uIssuerID;

	uint256&		uDirectTip		= pnCur.uDirectTip;
	uint256			uDirectEnd		= pnCur.uDirectEnd;
	bool&			bDirectAdvance	= pnCur.bDirectAdvance;
	SLE::pointer&	sleDirectDir	= pnCur.sleDirectDir;
	STAmount&		saOfrRate		= pnCur.saOfrRate;

	bool&			bEntryAdvance	= pnCur.bEntryAdvance;
	unsigned int&	uEntry			= pnCur.uEntry;
	uint256&		uOfferIndex		= pnCur.uOfferIndex;
	SLE::pointer&	sleOffer		= pnCur.sleOffer;
	uint160&		uOfrOwnerID		= pnCur.uOfrOwnerID;
	STAmount&		saOfferFunds	= pnCur.saOfferFunds;
	STAmount&		saTakerPays		= pnCur.saTakerPays;
	STAmount&		saTakerGets		= pnCur.saTakerGets;
	bool&			bFundsDirty		= pnCur.bFundsDirty;

	TER				terResult		= tesSUCCESS;

	do
	{
		bool	bDirectDirDirty	= false;

		if (!uDirectEnd)
		{
			// Need to initialize current node.

			uDirectTip		= Ledger::getBookBase(uPrvCurrencyID, uPrvIssuerID, uCurCurrencyID, uCurIssuerID);
			uDirectEnd		= Ledger::getQualityNext(uDirectTip);
			sleDirectDir	= lesActive.entryCache(ltDIR_NODE, uDirectTip);
			bDirectAdvance	= !sleDirectDir;
			bDirectDirDirty	= true;

			cLog(lsINFO) << boost::str(boost::format("calcNodeAdvance: Initialize node: uDirectTip=%s uDirectEnd=%s bDirectAdvance=%d") % uDirectTip % uDirectEnd % bDirectAdvance);
		}

		if (bDirectAdvance)
		{
			// Get next quality.
			uDirectTip		= lesActive.getLedger()->getNextLedgerIndex(uDirectTip, uDirectEnd);
			bDirectDirDirty	= true;
			bDirectAdvance	= false;

			if (!!uDirectTip)
			{
				// Have another quality directory.
				cLog(lsINFO) << boost::str(boost::format("calcNodeAdvance: Quality advance: uDirectTip=%s") % uDirectTip);

				sleDirectDir	= lesActive.entryCache(ltDIR_NODE, uDirectTip);
			}
			else if (bReverse)
			{
				cLog(lsINFO) << "calcNodeAdvance: No more offers.";

				uOfferIndex	= 0;
				break;
			}
			else
			{
				// No more offers. Should be done rather than fall off end of book.
				cLog(lsINFO) << "calcNodeAdvance: Unreachable: Fell off end of order book.";
				assert(false);

				terResult	= tefEXCEPTION;
			}
		}

		if (bDirectDirDirty)
		{
			saOfrRate		= STAmount::setRate(Ledger::getQuality(uDirectTip));	// For correct ratio
			uEntry			= 0;
			bEntryAdvance	= true;

			cLog(lsINFO) << boost::str(boost::format("calcNodeAdvance: directory dirty: saOfrRate=%s") % saOfrRate);
		}

		if (!bEntryAdvance)
		{
			if (bFundsDirty)
			{
				saTakerPays		= sleOffer->getFieldAmount(sfTakerPays);
				saTakerGets		= sleOffer->getFieldAmount(sfTakerGets);

				saOfferFunds	= lesActive.accountFunds(uOfrOwnerID, saTakerGets);	// Funds left.
				bFundsDirty		= false;

				cLog(lsINFO) << boost::str(boost::format("calcNodeAdvance: directory dirty: saOfrRate=%s") % saOfrRate);
			}
			else
			{
				cLog(lsINFO) << boost::str(boost::format("calcNodeAdvance: as is"));
				nothing();
			}
		}
		else if (!lesActive.dirNext(uDirectTip, sleDirectDir, uEntry, uOfferIndex))
		{
			// Failed to find an entry in directory.

			uOfferIndex	= 0;

			// Do another cur directory iff bMultiQuality
			if (bMultiQuality)
			{
				cLog(lsINFO) << boost::str(boost::format("calcNodeAdvance: next quality"));
				bDirectAdvance	= true;
			}
			else if (!bReverse)
			{
				cLog(lsINFO) << boost::str(boost::format("calcNodeAdvance: unreachable: ran out of offers"));
				assert(false);		// Can't run out of offers in forward direction.
				terResult	= tefEXCEPTION;
			}
		}
		else
		{
			// Got a new offer.
			sleOffer	= lesActive.entryCache(ltOFFER, uOfferIndex);
			uOfrOwnerID = sleOffer->getFieldAccount(sfAccount).getAccountID();

			const aciSource			asLine				= boost::make_tuple(uOfrOwnerID, uCurCurrencyID, uCurIssuerID);

			cLog(lsINFO) << boost::str(boost::format("calcNodeAdvance: uOfrOwnerID=%s") % NewcoinAddress::createHumanAccountID(uOfrOwnerID));

			if (sleOffer->isFieldPresent(sfExpiration) && sleOffer->getFieldU32(sfExpiration) <= lesActive.getLedger()->getParentCloseTimeNC())
			{
				// Offer is expired.
				cLog(lsINFO) << "calcNodeAdvance: expired offer";

				assert(musUnfundedFound.find(uOfferIndex) != musUnfundedFound.end());	// Verify reverse found it too.
				bEntryAdvance	= true;
				continue;
			}

			// Allowed to access source from this node?
			// XXX This can get called multiple times for same source in a row, caching result would be nice.
			curIssuerNodeConstIterator	itForward		= pspCur->umForward.find(asLine);
			const bool					bFoundForward	= itForward != pspCur->umForward.end();

			if (bFoundForward && itForward->second != uIndex)
			{
				// Temporarily unfunded. Another node uses this source, ignore in this offer.
				cLog(lsINFO) << "calcNodeAdvance: temporarily unfunded offer (forward)";

				bEntryAdvance	= true;
				continue;
			}

			curIssuerNodeConstIterator	itPast			= mumSource.find(asLine);
			bool						bFoundPast		= itPast != mumSource.end();

			if (bFoundPast && itPast->second != uIndex)
			{
				// Temporarily unfunded. Another node uses this source, ignore in this offer.
				cLog(lsINFO) << "calcNodeAdvance: temporarily unfunded offer (past)";

				bEntryAdvance	= true;
				continue;
			}

			curIssuerNodeConstIterator	itReverse		= pspCur->umReverse.find(asLine);
			bool						bFoundReverse	= itReverse != pspCur->umReverse.end();

			if (bFoundReverse && itReverse->second != uIndex)
			{
				// Temporarily unfunded. Another node uses this source, ignore in this offer.
				cLog(lsINFO) << "calcNodeAdvance: temporarily unfunded offer (reverse)";

				bEntryAdvance	= true;
				continue;
			}

			saTakerPays		= sleOffer->getFieldAmount(sfTakerPays);
			saTakerGets		= sleOffer->getFieldAmount(sfTakerGets);

			saOfferFunds	= lesActive.accountFunds(uOfrOwnerID, saTakerGets);	// Funds left.

			if (!saOfferFunds.isPositive())
			{
				// Offer is unfunded.
				cLog(lsINFO) << "calcNodeAdvance: unfunded offer";

				if (bReverse && !bFoundReverse && !bFoundPast)
				{
					// Never mentioned before: found unfunded.
					musUnfundedFound.insert(uOfferIndex);				// Mark offer for always deletion.
				}

				// YYY Could verify offer is correct place for unfundeds.
				bEntryAdvance	= true;
				continue;
			}

			if (bReverse			// Need to remember reverse mention.
				&& !bFoundPast		// Not mentioned in previous passes.
				&& !bFoundReverse)	// Not mentioned for pass.
			{
				// Consider source mentioned by current path state.
				cLog(lsINFO) << boost::str(boost::format("calcNodeAdvance: remember=%s/%s/%s")
					% NewcoinAddress::createHumanAccountID(uOfrOwnerID)
					% STAmount::createHumanCurrency(uCurCurrencyID)
					% NewcoinAddress::createHumanAccountID(uCurIssuerID));

				pspCur->umReverse.insert(std::make_pair(asLine, uIndex));
			}

			bFundsDirty		= false;
			bEntryAdvance	= false;
		}
	}
	while (tesSUCCESS == terResult && (bEntryAdvance || bDirectAdvance));

	if (tesSUCCESS == terResult)
	{
		cLog(lsINFO) << boost::str(boost::format("calcNodeAdvance: uOfferIndex=%s") % uOfferIndex);
	}
	else
	{
		cLog(lsINFO) << boost::str(boost::format("calcNodeAdvance: terResult=%s") % transToken(terResult));
	}

	return terResult;
}

// Between offer nodes, the fee charged may vary.  Therefore, process one inbound offer at a time.
// Propagate the inbound offer's requirements to the previous node.  The previous node adjusts the amount output and the
// amount spent on fees.
// Continue process till request is satisified while we the rate does not increase past the initial rate.
TER RippleCalc::calcNodeDeliverRev(
	const unsigned int			uIndex,			// 0 < uIndex < uLast
	const PathState::pointer&	pspCur,
	const bool					bMultiQuality,
	const uint160&				uOutAccountID,	// --> Output owner's account.
	const STAmount&				saOutReq,		// --> Funds wanted.
	STAmount&					saOutAct)		// <-- Funds delivered.
{
	TER	terResult	= tesSUCCESS;

	PaymentNode&	pnPrv			= pspCur->vpnNodes[uIndex-1];
	PaymentNode&	pnCur			= pspCur->vpnNodes[uIndex];

	const uint160&	uCurIssuerID	= pnCur.uIssuerID;
	const uint160&	uPrvAccountID	= pnPrv.uAccountID;
	const STAmount&	saTransferRate	= pnCur.saTransferRate;

	STAmount&		saPrvDlvReq		= pnPrv.saRevDeliver;	// To be adjusted.

	saOutAct	= 0;

	while (saOutAct != saOutReq)							// Did not deliver limit.
	{
		bool&			bEntryAdvance	= pnCur.bEntryAdvance;
		STAmount&		saOfrRate		= pnCur.saOfrRate;
		uint256&		uOfferIndex		= pnCur.uOfferIndex;
		SLE::pointer&	sleOffer		= pnCur.sleOffer;
		const uint160&	uOfrOwnerID		= pnCur.uOfrOwnerID;
		bool&			bFundsDirty		= pnCur.bFundsDirty;
		STAmount&		saOfferFunds	= pnCur.saOfferFunds;
		STAmount&		saTakerPays		= pnCur.saTakerPays;
		STAmount&		saTakerGets		= pnCur.saTakerGets;
		STAmount&		saRateMax		= pnCur.saRateMax;

		terResult	= calcNodeAdvance(uIndex, pspCur, bMultiQuality, true);		// If needed, advance to next funded offer.

		if (tesSUCCESS != terResult || !uOfferIndex)
		{
			// Error or out of offers.
			break;
		}

		const STAmount	saOutFeeRate	= uOfrOwnerID == uCurIssuerID || uOutAccountID == uCurIssuerID // Issuer receiving or sending.
										? saOne				// No fee.
										: saTransferRate;	// Transfer rate of issuer.
		cLog(lsINFO) << boost::str(boost::format("calcNodeDeliverRev: uOfrOwnerID=%s uOutAccountID=%s uCurIssuerID=%s saTransferRate=%s saOutFeeRate=%s")
			% NewcoinAddress::createHumanAccountID(uOfrOwnerID)
			% NewcoinAddress::createHumanAccountID(uOutAccountID)
			% NewcoinAddress::createHumanAccountID(uCurIssuerID)
			% saTransferRate.getFullText()
			% saOutFeeRate.getFullText());

		if (!saRateMax)
		{
			// Set initial rate.
			saRateMax	= saOutFeeRate;

			cLog(lsINFO) << boost::str(boost::format("calcNodeDeliverRev: Set initial rate: saRateMax=%s saOutFeeRate=%s")
				% saRateMax
				% saOutFeeRate);
		}
		else if (saRateMax < saOutFeeRate)
		{
			// Offer exceeds initial rate.
			cLog(lsINFO) << boost::str(boost::format("calcNodeDeliverRev: Offer exceeds initial rate: saRateMax=%s saOutFeeRate=%s")
				% saRateMax
				% saOutFeeRate);

			nothing();
			break;
		}
		else if (saOutFeeRate < saRateMax)
		{
			// Reducing rate.

			saRateMax	= saOutFeeRate;

			cLog(lsINFO) << boost::str(boost::format("calcNodeDeliverRev: Reducing rate: saRateMax=%s")
				% saRateMax);
		}

		STAmount	saOutPass		= std::min(std::min(saOfferFunds, saTakerGets), saOutReq-saOutAct);	// Offer maximum out - assuming no out fees.
		STAmount	saOutPlusFees	= STAmount::multiply(saOutPass, saOutFeeRate);						// Offer out with fees.

		cLog(lsINFO) << boost::str(boost::format("calcNodeDeliverRev: saOutReq=%s saOutAct=%s saTakerGets=%s saOutPass=%s saOutPlusFees=%s saOfferFunds=%s")
			% saOutReq
			% saOutAct
			% saTakerGets
			% saOutPass
			% saOutPlusFees
			% saOfferFunds);

		if (saOutPlusFees > saOfferFunds)
		{
			// Offer owner can not cover all fees, compute saOutPass based on saOfferFunds.

			saOutPlusFees	= saOfferFunds;
			saOutPass		= STAmount::divide(saOutPlusFees, saOutFeeRate);

			cLog(lsINFO) << boost::str(boost::format("calcNodeDeliverRev: Total exceeds fees: saOutPass=%s saOutPlusFees=%s saOfferFunds=%s")
				% saOutPass
				% saOutPlusFees
				% saOfferFunds);
		}

		// Compute portion of input needed to cover output.

		STAmount	saInPassReq	= STAmount::multiply(saOutPass, saOfrRate, saTakerPays);
		STAmount	saInPassAct;

		cLog(lsINFO) << boost::str(boost::format("calcNodeDeliverRev: saInPassReq=%s saOfrRate=%s saOutPass=%s saOutPlusFees=%s")
			% saInPassReq
			% saOfrRate
			% saOutPass
			% saOutPlusFees);

		// Find out input amount actually available at current rate.
		if (!!uPrvAccountID)
		{
			// account --> OFFER --> ?
			// Previous is the issuer and receiver is an offer, so no fee or quality.
			// Previous is the issuer and has unlimited funds.
			// Offer owner is obtaining IOUs via an offer, so credit line limits are ignored.
			// As limits are ignored, don't need to adjust previous account's balance.

			saInPassAct	= saInPassReq;

			cLog(lsINFO) << boost::str(boost::format("calcNodeDeliverRev: account --> OFFER --> ? : saInPassAct=%s")
				% saPrvDlvReq);
		}
		else
		{
			// offer --> OFFER --> ?

			terResult	= calcNodeDeliverRev(
				uIndex-1,
				pspCur,
				bMultiQuality,
				uOfrOwnerID,
				saInPassReq,
				saInPassAct);

			cLog(lsINFO) << boost::str(boost::format("calcNodeDeliverRev: offer --> OFFER --> ? : saInPassAct=%s")
				% saInPassAct);
		}

		if (tesSUCCESS != terResult)
			break;

		if (saInPassAct != saInPassReq)
		{
			// Adjust output to conform to limited input.
			saOutPass		= STAmount::divide(saInPassAct, saOfrRate, saTakerGets);
			saOutPlusFees	= STAmount::multiply(saOutPass, saOutFeeRate);

			cLog(lsINFO) << boost::str(boost::format("calcNodeDeliverRev: adjusted: saOutPass=%s saOutPlusFees=%s")
				% saOutPass
				% saOutPlusFees);
		}

		// Funds were spent.
		bFundsDirty		= true;

		// Deduct output, don't actually need to send.
		lesActive.accountSend(uOfrOwnerID, uCurIssuerID, saOutPass);

		// Adjust offer
		sleOffer->setFieldAmount(sfTakerGets, saTakerGets - saOutPass);
		sleOffer->setFieldAmount(sfTakerPays, saTakerPays - saInPassAct);

		lesActive.entryModify(sleOffer);

		if (saOutPass == saTakerGets)
		{
			// Offer became unfunded.
			cLog(lsINFO) << boost::str(boost::format("calcNodeDeliverRev: offer became unfunded."));

			bEntryAdvance	= true;
		}

		saOutAct	+= saOutPass;
		saPrvDlvReq	+= saInPassAct;
	}

	if (!saOutAct)
		terResult	= tepPATH_DRY;

	return terResult;
}

// Deliver maximum amount of funds from previous node.
// Goal: Make progress consuming the offer.
TER RippleCalc::calcNodeDeliverFwd(
	const unsigned int			uIndex,			// 0 < uIndex < uLast
	const PathState::pointer&	pspCur,
	const bool					bMultiQuality,
	const uint160&				uInAccountID,	// --> Input owner's account.
	const STAmount&				saInFunds,		// --> Funds available for delivery and fees.
	const STAmount&				saInReq,		// --> Limit to deliver.
	STAmount&					saInAct,		// <-- Amount delivered.
	STAmount&					saInFees)		// <-- Fees charged.
{
	TER	terResult	= tesSUCCESS;

	PaymentNode&	pnPrv			= pspCur->vpnNodes[uIndex-1];
	PaymentNode&	pnCur			= pspCur->vpnNodes[uIndex];
	PaymentNode&	pnNxt			= pspCur->vpnNodes[uIndex+1];

	const uint160&	uNxtAccountID	= pnNxt.uAccountID;
	const uint160&	uCurIssuerID	= pnCur.uIssuerID;
	const uint160&	uPrvIssuerID	= pnPrv.uIssuerID;
	const STAmount&	saTransferRate	= pnPrv.saTransferRate;

	STAmount&		saCurDeliverAct	= pnCur.saFwdDeliver;

	saInAct		= 0;
	saInFees	= 0;

	while (tesSUCCESS == terResult
		&& saInAct != saInReq					// Did not deliver limit.
		&& saInAct + saInFees != saInFunds)		// Did not deliver all funds.
	{
		terResult	= calcNodeAdvance(uIndex, pspCur, bMultiQuality, false);				// If needed, advance to next funded offer.

		if (tesSUCCESS == terResult)
		{
			bool&			bEntryAdvance	= pnCur.bEntryAdvance;
			STAmount&		saOfrRate		= pnCur.saOfrRate;
			uint256&		uOfferIndex		= pnCur.uOfferIndex;
			SLE::pointer&	sleOffer		= pnCur.sleOffer;
			const uint160&	uOfrOwnerID		= pnCur.uOfrOwnerID;
			bool&			bFundsDirty		= pnCur.bFundsDirty;
			STAmount&		saOfferFunds	= pnCur.saOfferFunds;
			STAmount&		saTakerPays		= pnCur.saTakerPays;
			STAmount&		saTakerGets		= pnCur.saTakerGets;


			const STAmount	saInFeeRate	= uInAccountID == uPrvIssuerID || uOfrOwnerID == uPrvIssuerID	// Issuer receiving or sending.
											? saOne				// No fee.
											: saTransferRate;	// Transfer rate of issuer.

			//
			// First calculate assuming no output fees.
			// XXX Make sure derived in does not exceed actual saTakerPays due to rounding.

			STAmount	saOutFunded		= std::max(saOfferFunds, saTakerGets);						// Offer maximum out - There are no out fees.
			STAmount	saInFunded		= STAmount::multiply(saOutFunded, saOfrRate, saInReq);		// Offer maximum in - Limited by by payout.
			STAmount	saInTotal		= STAmount::multiply(saInFunded, saTransferRate);			// Offer maximum in with fees.
			STAmount	saInSum			= std::min(saInTotal, saInFunds-saInAct-saInFees);			// In limited by saInFunds.
			STAmount	saInPassAct		= STAmount::divide(saInSum, saInFeeRate);					// In without fees.
			STAmount	saOutPassMax	= STAmount::divide(saInPassAct, saOfrRate, saOutFunded);	// Out.

			STAmount	saInPassFees;
			STAmount	saOutPassAct;

			cLog(lsINFO) << boost::str(boost::format("calcNodeDeliverFwd: saOutFunded=%s saInFunded=%s saInTotal=%s saInSum=%s saInPassAct=%s saOutPassMax=%s")
				% saOutFunded
				% saInFunded
				% saInTotal
				% saInSum
				% saInPassAct
				% saOutPassMax);

			if (!!uNxtAccountID)
			{
				// ? --> OFFER --> account
				// Input fees: vary based upon the consumed offer's owner.
				// Output fees: none as the destination account is the issuer.

				// XXX This doesn't claim input.
				// XXX Assumes input is in limbo.  XXX Check.

				// Debit offer owner.
				lesActive.accountSend(uOfrOwnerID, uCurIssuerID, saOutPassMax);

				saOutPassAct	= saOutPassMax;

				cLog(lsINFO) << boost::str(boost::format("calcNodeDeliverFwd: ? --> OFFER --> account: saOutPassAct=%s")
					% saOutPassAct);
			}
			else
			{
				// ? --> OFFER --> offer
				STAmount	saOutPassFees;

				terResult	= RippleCalc::calcNodeDeliverFwd(
					uIndex+1,
					pspCur,
					bMultiQuality,
					uOfrOwnerID,
					saOutPassMax,
					saOutPassMax,
					saOutPassAct,		// <-- Amount delivered.
					saOutPassFees);		// <-- Fees charged.

				if (tesSUCCESS != terResult)
					break;

				// Offer maximum in limited by next payout.
				saInPassAct			= STAmount::multiply(saOutPassAct, saOfrRate);
				saInPassFees		= STAmount::multiply(saInFunded, saInFeeRate)-saInPassAct;
			}

			cLog(lsINFO) << boost::str(boost::format("calcNodeDeliverFwd: saTakerGets=%s saTakerPays=%s saInPassAct=%s saOutPassAct=%s")
				% saTakerGets.getFullText()
				% saTakerPays.getFullText()
				% saInPassAct.getFullText()
				% saOutPassAct.getFullText());

			// Funds were spent.
			bFundsDirty		= true;

			// Credit issuer transfer fees.
			lesActive.accountSend(uInAccountID, uOfrOwnerID, saInPassFees);

			// Credit offer owner from offer.
			lesActive.accountSend(uInAccountID, uOfrOwnerID, saInPassAct);

			// Adjust offer
			sleOffer->setFieldAmount(sfTakerGets, saTakerGets - saOutPassAct);
			sleOffer->setFieldAmount(sfTakerPays, saTakerPays - saInPassAct);

			lesActive.entryModify(sleOffer);

			if (saOutPassAct == saTakerGets)
			{
				// Offer became unfunded.
				pspCur->vUnfundedBecame.push_back(uOfferIndex);
				bEntryAdvance	= true;
			}

			saInAct			+= saInPassAct;
			saInFees		+= saInPassFees;

			// Adjust amount available to next node.
			saCurDeliverAct	+= saOutPassAct;
		}
	}

	return terResult;
}

// Called to drive from the last offer node in a chain.
TER RippleCalc::calcNodeOfferRev(
	const unsigned int			uIndex,				// 0 < uIndex < uLast
	const PathState::pointer&	pspCur,
	const bool					bMultiQuality)
{
	TER				terResult;

	PaymentNode&	pnCur			= pspCur->vpnNodes[uIndex];
	PaymentNode&	pnNxt			= pspCur->vpnNodes[uIndex+1];

	if (!!pnNxt.uAccountID)
	{
		// Next is an account node, resolve current offer node's deliver.
		STAmount		saDeliverAct;

		terResult	= calcNodeDeliverRev(
							uIndex,
							pspCur,
							bMultiQuality,

							pnNxt.uAccountID,
							pnCur.saRevDeliver,
							saDeliverAct);
	}
	else
	{
		// Next is an offer. Deliver has already been resolved.
		terResult	= tesSUCCESS;
	}

	return terResult;
}

// Called to drive the from the first offer node in a chain.
// - Offer input is limbo.
// - Current offers consumed.
//   - Current offer owners debited.
//   - Transfer fees credited to issuer.
//   - Payout to issuer or limbo.
// - Deliver is set without transfer fees.
TER RippleCalc::calcNodeOfferFwd(
	const unsigned int			uIndex,				// 0 < uIndex < uLast
	const PathState::pointer&	pspCur,
	const bool					bMultiQuality
	)
{
	TER				terResult;
	PaymentNode&	pnPrv			= pspCur->vpnNodes[uIndex-1];

	if (!!pnPrv.uAccountID)
	{
		// Previous is an account node, resolve its deliver.
		STAmount		saInAct;
		STAmount		saInFees;

		terResult	= calcNodeDeliverFwd(
							uIndex,
							pspCur,
							bMultiQuality,
							pnPrv.uAccountID,
							pnPrv.saFwdDeliver,
							pnPrv.saFwdDeliver,
							saInAct,
							saInFees);

		assert(tesSUCCESS != terResult || pnPrv.saFwdDeliver == saInAct+saInFees);
	}
	else
	{
		// Previous is an offer. Deliver has already been resolved.
		terResult	= tesSUCCESS;
	}

	return terResult;

}

// Cur is the driver and will be filled exactly.
// uQualityIn -> uQualityOut
//   saPrvReq -> saCurReq
//   sqPrvAct -> saCurAct
// This is a minimizing routine: moving in reverse it propagates the send limit to the sender, moving forward it propagates the
// actual send toward the receiver.
// This routine works backwards as it calculates previous wants based on previous credit limits and current wants.
// This routine works forwards as it calculates current deliver based on previous delivery limits and current wants.
// XXX Deal with uQualityIn or uQualityOut = 0
void RippleCalc::calcNodeRipple(
	const uint32 uQualityIn,
	const uint32 uQualityOut,
	const STAmount& saPrvReq,	// --> in limit including fees, <0 = unlimited
	const STAmount& saCurReq,	// --> out limit (driver)
	STAmount& saPrvAct,			// <-> in limit including achieved
	STAmount& saCurAct,			// <-> out limit achieved.
	uint64& uRateMax)
{
	cLog(lsINFO) << boost::str(boost::format("calcNodeRipple> uQualityIn=%d uQualityOut=%d saPrvReq=%s saCurReq=%s saPrvAct=%s saCurAct=%s")
		% uQualityIn
		% uQualityOut
		% saPrvReq.getFullText()
		% saCurReq.getFullText()
		% saPrvAct.getFullText()
		% saCurAct.getFullText());

	assert(saPrvReq.getCurrency() == saCurReq.getCurrency());

	const bool		bPrvUnlimited	= saPrvReq.isNegative();
	const STAmount	saPrv			= bPrvUnlimited ? STAmount(saPrvReq) : saPrvReq-saPrvAct;
	const STAmount	saCur			= saCurReq-saCurAct;

#if 0
	cLog(lsINFO) << boost::str(boost::format("calcNodeRipple: bPrvUnlimited=%d saPrv=%s saCur=%s")
		% bPrvUnlimited
		% saPrv.getFullText()
		% saCur.getFullText());
#endif

	if (uQualityIn >= uQualityOut)
	{
		// No fee.
		cLog(lsINFO) << boost::str(boost::format("calcNodeRipple: No fees"));

		if (!uRateMax || STAmount::uRateOne <= uRateMax)
		{
			STAmount	saTransfer	= bPrvUnlimited ? saCur : std::min(saPrv, saCur);

			saPrvAct	+= saTransfer;
			saCurAct	+= saTransfer;

			if (!uRateMax)
				uRateMax	= STAmount::uRateOne;
		}
	}
	else
	{
		// Fee.
		cLog(lsINFO) << boost::str(boost::format("calcNodeRipple: Fee"));

		uint64	uRate	= STAmount::getRate(STAmount(uQualityIn), STAmount(uQualityOut));

		if (!uRateMax || uRate <= uRateMax)
		{
			const uint160	uCurrencyID		= saCur.getCurrency();
			const uint160	uCurIssuerID	= saCur.getIssuer();
			// const uint160	uPrvIssuerID	= saPrv.getIssuer();

			STAmount	saCurIn		= STAmount::divide(STAmount::multiply(saCur, uQualityOut, uCurrencyID, uCurIssuerID), uQualityIn, uCurrencyID, uCurIssuerID);

	cLog(lsINFO) << boost::str(boost::format("calcNodeRipple: bPrvUnlimited=%d saPrv=%s saCurIn=%s") % bPrvUnlimited % saPrv.getFullText() % saCurIn.getFullText());
			if (bPrvUnlimited || saCurIn <= saPrv)
			{
				// All of cur. Some amount of prv.
				saCurAct	+= saCur;
				saPrvAct	+= saCurIn;
	cLog(lsINFO) << boost::str(boost::format("calcNodeRipple:3c: saCurReq=%s saPrvAct=%s") % saCurReq.getFullText() % saPrvAct.getFullText());
			}
			else
			{
				// A part of cur. All of prv. (cur as driver)
				STAmount	saCurOut	= STAmount::divide(STAmount::multiply(saPrv, uQualityIn, uCurrencyID, uCurIssuerID), uQualityOut, uCurrencyID, uCurIssuerID);
	cLog(lsINFO) << boost::str(boost::format("calcNodeRipple:4: saCurReq=%s") % saCurReq.getFullText());

				saCurAct	+= saCurOut;
				saPrvAct	= saPrvReq;

				if (!uRateMax)
					uRateMax	= uRate;
			}
		}
	}

	cLog(lsINFO) << boost::str(boost::format("calcNodeRipple< uQualityIn=%d uQualityOut=%d saPrvReq=%s saCurReq=%s saPrvAct=%s saCurAct=%s")
		% uQualityIn
		% uQualityOut
		% saPrvReq.getFullText()
		% saCurReq.getFullText()
		% saPrvAct.getFullText()
		% saCurAct.getFullText());
}

// Calculate saPrvRedeemReq, saPrvIssueReq, saPrvDeliver from saCur...
// <-- tesSUCCESS or tepPATH_DRY
TER RippleCalc::calcNodeAccountRev(const unsigned int uIndex, const PathState::pointer& pspCur, const bool bMultiQuality)
{
	TER					terResult		= tesSUCCESS;
	const unsigned int	uLast			= pspCur->vpnNodes.size() - 1;

	uint64				uRateMax		= 0;

	PaymentNode&		pnPrv			= pspCur->vpnNodes[uIndex ? uIndex-1 : 0];
	PaymentNode&		pnCur			= pspCur->vpnNodes[uIndex];
	PaymentNode&		pnNxt			= pspCur->vpnNodes[uIndex == uLast ? uLast : uIndex+1];

	// Current is allowed to redeem to next.
	const bool			bPrvAccount		= !uIndex || isSetBit(pnPrv.uFlags, STPathElement::typeAccount);
	const bool			bNxtAccount		= uIndex == uLast || isSetBit(pnNxt.uFlags, STPathElement::typeAccount);

	const uint160&		uCurAccountID	= pnCur.uAccountID;
	const uint160&		uPrvAccountID	= bPrvAccount ? pnPrv.uAccountID : uCurAccountID;
	const uint160&		uNxtAccountID	= bNxtAccount ? pnNxt.uAccountID : uCurAccountID;	// Offers are always issue.

	const uint160&		uCurrencyID		= pnCur.uCurrencyID;

	const uint32		uQualityIn		= uIndex ? lesActive.rippleQualityIn(uCurAccountID, uPrvAccountID, uCurrencyID) : QUALITY_ONE;
	const uint32		uQualityOut		= uIndex != uLast ? lesActive.rippleQualityOut(uCurAccountID, uNxtAccountID, uCurrencyID) : QUALITY_ONE;

	// For bPrvAccount
	const STAmount		saPrvOwed		= bPrvAccount && uIndex								// Previous account is owed.
											? lesActive.rippleOwed(uCurAccountID, uPrvAccountID, uCurrencyID)
											: STAmount(uCurrencyID, uCurAccountID);

	const STAmount		saPrvLimit		= bPrvAccount && uIndex								// Previous account may owe.
											? lesActive.rippleLimit(uCurAccountID, uPrvAccountID, uCurrencyID)
											: STAmount(uCurrencyID, uCurAccountID);

	const STAmount		saNxtOwed		= bNxtAccount && uIndex != uLast					// Next account is owed.
											? lesActive.rippleOwed(uCurAccountID, uNxtAccountID, uCurrencyID)
											: STAmount(uCurrencyID, uCurAccountID);

	cLog(lsINFO) << boost::str(boost::format("calcNodeAccountRev> uIndex=%d/%d uPrvAccountID=%s uCurAccountID=%s uNxtAccountID=%s uCurrencyID=%s uQualityIn=%d uQualityOut=%d saPrvOwed=%s saPrvLimit=%s")
		% uIndex
		% uLast
		% NewcoinAddress::createHumanAccountID(uPrvAccountID)
		% NewcoinAddress::createHumanAccountID(uCurAccountID)
		% NewcoinAddress::createHumanAccountID(uNxtAccountID)
		% STAmount::createHumanCurrency(uCurrencyID)
		% uQualityIn
		% uQualityOut
		% saPrvOwed.getFullText()
		% saPrvLimit.getFullText());

	// Previous can redeem the owed IOUs it holds.
	const STAmount	saPrvRedeemReq	= saPrvOwed.isPositive() ? saPrvOwed : STAmount(uCurrencyID, 0);
	STAmount&		saPrvRedeemAct	= pnPrv.saRevRedeem;

	// Previous can issue up to limit minus whatever portion of limit already used (not including redeemable amount).
	const STAmount	saPrvIssueReq	= saPrvOwed.isNegative() ? saPrvLimit+saPrvOwed : saPrvLimit;
	STAmount&		saPrvIssueAct	= pnPrv.saRevIssue;

	// For !bPrvAccount
	const STAmount	saPrvDeliverReq	= STAmount::saFromSigned(uCurrencyID, uCurAccountID, -1);	// Unlimited.
	STAmount&		saPrvDeliverAct	= pnPrv.saRevDeliver;

	// For bNxtAccount
	const STAmount&	saCurRedeemReq	= pnCur.saRevRedeem;
	STAmount		saCurRedeemAct(saCurRedeemReq.getCurrency(), saCurRedeemReq.getIssuer());

	const STAmount&	saCurIssueReq	= pnCur.saRevIssue;
	STAmount		saCurIssueAct(saCurIssueReq.getCurrency(), saCurIssueReq.getIssuer());					// Track progress.

	// For !bNxtAccount
	const STAmount&	saCurDeliverReq	= pnCur.saRevDeliver;
	STAmount		saCurDeliverAct(saCurDeliverReq.getCurrency(), saCurDeliverReq.getIssuer());

	cLog(lsINFO) << boost::str(boost::format("calcNodeAccountRev: saPrvRedeemReq=%s saPrvIssueReq=%s saCurRedeemReq=%s saCurIssueReq=%s saNxtOwed=%s")
		% saPrvRedeemReq.getFullText()
		% saPrvIssueReq.getFullText()
		% saCurRedeemReq.getFullText()
		% saCurIssueReq.getFullText()
		% saNxtOwed.getFullText());

	cLog(lsINFO) << pspCur->getJson();

	assert(!saCurRedeemReq || (-saNxtOwed) >= saCurRedeemReq);	// Current redeem req can't be more than IOUs on hand.
	assert(!saCurIssueReq					// If not issuing, fine.
		|| !saNxtOwed.isNegative()			// saNxtOwed >= 0: Sender not holding next IOUs, saNxtOwed < 0: Sender holding next IOUs.
		|| -saNxtOwed == saCurRedeemReq);	// If issue req, then redeem req must consume all owed.

	if (bPrvAccount && bNxtAccount)
	{
		if (!uIndex)
		{
			// ^ --> ACCOUNT -->  account|offer
			// Nothing to do, there is no previous to adjust.
			nothing();
		}
		else if (uIndex == uLast)
		{
			// account --> ACCOUNT --> $
			// Overall deliverable.
			const STAmount&	saCurWantedReq	= bPrvAccount
												? std::min(pspCur->saOutReq-pspCur->saOutAct, saPrvLimit+saPrvOwed)	// If previous is an account, limit.
												: pspCur->saOutReq-pspCur->saOutAct;								// Previous is an offer, no limit: redeem own IOUs.
			STAmount		saCurWantedAct(saCurWantedReq.getCurrency(), saCurWantedReq.getIssuer());

			cLog(lsINFO) << boost::str(boost::format("calcNodeAccountRev: account --> ACCOUNT --> $ : saCurWantedReq=%s")
				% saCurWantedReq.getFullText());

			// Calculate redeem
			if (saPrvRedeemReq)							// Previous has IOUs to redeem.
			{
				// Redeem at 1:1
				cLog(lsINFO) << boost::str(boost::format("calcNodeAccountRev: Redeem at 1:1"));

				saCurWantedAct		= std::min(saPrvRedeemReq, saCurWantedReq);
				saPrvRedeemAct		= saCurWantedAct;

				uRateMax			= STAmount::uRateOne;
			}

			// Calculate issuing.
			if (saCurWantedReq != saCurWantedAct		// Need more.
				&& saPrvIssueReq)						// Will accept IOUs from prevous.
			{
				// Rate: quality in : 1.0
				cLog(lsINFO) << boost::str(boost::format("calcNodeAccountRev: Rate: quality in : 1.0"));

				// If we previously redeemed and this has a poorer rate, this won't be included the current increment.
				calcNodeRipple(uQualityIn, QUALITY_ONE, saPrvIssueReq, saCurWantedReq, saPrvIssueAct, saCurWantedAct, uRateMax);
			}

			if (!saCurWantedAct)
			{
				// Must have processed something.
				terResult	= tepPATH_DRY;
			}
		}
		else
		{
			// ^|account --> ACCOUNT --> account

			// redeem (part 1) -> redeem
			if (saCurRedeemReq							// Next wants IOUs redeemed.
				&& saPrvRedeemReq)						// Previous has IOUs to redeem.
			{
				// Rate : 1.0 : quality out
				cLog(lsINFO) << boost::str(boost::format("calcNodeAccountRev: Rate : 1.0 : quality out"));

				calcNodeRipple(QUALITY_ONE, uQualityOut, saPrvRedeemReq, saCurRedeemReq, saPrvRedeemAct, saCurRedeemAct, uRateMax);
			}

			// issue (part 1) -> redeem
			if (saCurRedeemReq != saCurRedeemAct		// Next wants more IOUs redeemed.
				&& saPrvRedeemAct == saPrvRedeemReq)	// Previous has no IOUs to redeem remaining.
			{
				// Rate: quality in : quality out
				cLog(lsINFO) << boost::str(boost::format("calcNodeAccountRev: Rate: quality in : quality out"));

				calcNodeRipple(uQualityIn, uQualityOut, saPrvIssueReq, saCurRedeemReq, saPrvIssueAct, saCurRedeemAct, uRateMax);
			}

			// redeem (part 2) -> issue.
			if (saCurIssueReq							// Next wants IOUs issued.
				&& saCurRedeemAct == saCurRedeemReq		// Can only issue if completed redeeming.
				&& saPrvRedeemAct != saPrvRedeemReq)	// Did not complete redeeming previous IOUs.
			{
				// Rate : 1.0 : transfer_rate
				cLog(lsINFO) << boost::str(boost::format("calcNodeAccountRev: Rate : 1.0 : transfer_rate"));

				calcNodeRipple(QUALITY_ONE, lesActive.rippleTransferRate(uCurAccountID), saPrvRedeemReq, saCurIssueReq, saPrvRedeemAct, saCurIssueAct, uRateMax);
			}

			// issue (part 2) -> issue
			if (saCurIssueReq != saCurIssueAct			// Need wants more IOUs issued.
				&& saCurRedeemAct == saCurRedeemReq		// Can only issue if completed redeeming.
				&& saPrvRedeemReq == saPrvRedeemAct)	// Previously redeemed all owed IOUs.
			{
				// Rate: quality in : 1.0
				cLog(lsINFO) << boost::str(boost::format("calcNodeAccountRev: Rate: quality in : 1.0"));

				calcNodeRipple(uQualityIn, QUALITY_ONE, saPrvIssueReq, saCurIssueReq, saPrvIssueAct, saCurIssueAct, uRateMax);
			}

			if (!saCurRedeemAct && !saCurIssueAct)
			{
				// Must want something.
				terResult	= tepPATH_DRY;
			}

			cLog(lsINFO) << boost::str(boost::format("calcNodeAccountRev: ^|account --> ACCOUNT --> account : saCurRedeemReq=%s saCurIssueReq=%s saPrvOwed=%s saCurRedeemAct=%s saCurIssueAct=%s")
				% saCurRedeemReq.getFullText()
				% saCurIssueReq.getFullText()
				% saPrvOwed.getFullText()
				% saCurRedeemAct.getFullText()
				% saCurIssueAct.getFullText());
		}
	}
	else if (bPrvAccount && !bNxtAccount)
	{
		// account --> ACCOUNT --> offer
		// Note: deliver is always issue as ACCOUNT is the issuer for the offer input.
		cLog(lsINFO) << boost::str(boost::format("calcNodeAccountRev: account --> ACCOUNT --> offer"));

		// redeem -> deliver/issue.
		if (saPrvOwed.isPositive()					// Previous has IOUs to redeem.
			&& saCurDeliverReq)						// Need some issued.
		{
			// Rate : 1.0 : transfer_rate
			calcNodeRipple(QUALITY_ONE, lesActive.rippleTransferRate(uCurAccountID), saPrvRedeemReq, saCurDeliverReq, saPrvRedeemAct, saCurDeliverAct, uRateMax);
		}

		// issue -> deliver/issue
		if (saPrvRedeemReq == saPrvRedeemAct		// Previously redeemed all owed.
			&& saCurDeliverReq != saCurDeliverAct)	// Still need some issued.
		{
			// Rate: quality in : 1.0
			calcNodeRipple(uQualityIn, QUALITY_ONE, saPrvIssueReq, saCurDeliverReq, saPrvIssueAct, saCurDeliverAct, uRateMax);
		}

		if (!saCurDeliverAct)
		{
			// Must want something.
			terResult	= tepPATH_DRY;
		}

		cLog(lsINFO) << boost::str(boost::format("calcNodeAccountRev: saCurDeliverReq=%s saCurDeliverAct=%s saPrvOwed=%s")
			% saCurDeliverReq.getFullText()
			% saCurDeliverAct.getFullText()
			% saPrvOwed.getFullText());
	}
	else if (!bPrvAccount && bNxtAccount)
	{
		if (uIndex == uLast)
		{
			// offer --> ACCOUNT --> $
			const STAmount&	saCurWantedReq	= bPrvAccount
												? std::min(pspCur->saOutReq-pspCur->saOutAct, saPrvLimit+saPrvOwed)	// If previous is an account, limit.
												: pspCur->saOutReq-pspCur->saOutAct;								// Previous is an offer, no limit: redeem own IOUs.
			STAmount		saCurWantedAct(saCurWantedReq.getCurrency(), saCurWantedReq.getIssuer());

			cLog(lsINFO) << boost::str(boost::format("calcNodeAccountRev: offer --> ACCOUNT --> $ : saCurWantedReq=%s")
				% saCurWantedReq.getFullText());

			// Rate: quality in : 1.0
			calcNodeRipple(uQualityIn, QUALITY_ONE, saPrvDeliverReq, saCurWantedReq, saPrvDeliverAct, saCurWantedAct, uRateMax);

			if (!saCurWantedAct)
			{
				// Must have processed something.
				terResult	= tepPATH_DRY;
			}
		}
		else
		{
			// offer --> ACCOUNT --> account
			// Note: offer is always delivering(redeeming) as account is issuer.
			cLog(lsINFO) << boost::str(boost::format("calcNodeAccountRev: offer --> ACCOUNT --> account"));

			// deliver -> redeem
			if (saCurRedeemReq)							// Next wants us to redeem.
			{
				// Rate : 1.0 : quality out
				calcNodeRipple(QUALITY_ONE, uQualityOut, saPrvDeliverReq, saCurRedeemReq, saPrvDeliverAct, saCurRedeemAct, uRateMax);
			}

			// deliver -> issue.
			if (saCurRedeemReq == saCurRedeemAct		// Can only issue if previously redeemed all.
				&& saCurIssueReq)						// Need some issued.
			{
				// Rate : 1.0 : transfer_rate
				calcNodeRipple(QUALITY_ONE, lesActive.rippleTransferRate(uCurAccountID), saPrvDeliverReq, saCurIssueReq, saPrvDeliverAct, saCurIssueAct, uRateMax);
			}

			cLog(lsINFO) << boost::str(boost::format("calcNodeAccountRev: saCurRedeemReq=%s saCurIssueAct=%s saCurIssueReq=%s saPrvDeliverAct=%s")
				% saCurRedeemReq.getFullText()
				% saCurRedeemAct.getFullText()
				% saCurIssueReq.getFullText()
				% saPrvDeliverAct.getFullText());

			if (!saPrvDeliverAct)
			{
				// Must want something.
				terResult	= tepPATH_DRY;
			}
		}
	}
	else
	{
		// offer --> ACCOUNT --> offer
		// deliver/redeem -> deliver/issue.
		cLog(lsINFO) << boost::str(boost::format("calcNodeAccountRev: offer --> ACCOUNT --> offer"));

		// Rate : 1.0 : transfer_rate
		calcNodeRipple(QUALITY_ONE, lesActive.rippleTransferRate(uCurAccountID), saPrvDeliverReq, saCurDeliverReq, saPrvDeliverAct, saCurDeliverAct, uRateMax);

		if (!saCurDeliverAct)
		{
			// Must want something.
			terResult	= tepPATH_DRY;
		}
	}

	return terResult;
}

// Perform balance adjustments between previous and current node.
// - The previous node: specifies what to push through to current.
// - All of previous output is consumed.
// Then, compute current node's output for next node.
// - Current node: specify what to push through to next.
// - Output to next node is computed as input minus quality or transfer fee.
TER RippleCalc::calcNodeAccountFwd(
	const unsigned int			uIndex,				// 0 <= uIndex <= uLast
	const PathState::pointer&	pspCur,
	const bool					bMultiQuality)
{
	TER					terResult		= tesSUCCESS;
	const unsigned int	uLast			= pspCur->vpnNodes.size() - 1;

	uint64				uRateMax		= 0;

	PaymentNode&	pnPrv			= pspCur->vpnNodes[uIndex ? uIndex-1 : 0];
	PaymentNode&	pnCur			= pspCur->vpnNodes[uIndex];
	PaymentNode&	pnNxt			= pspCur->vpnNodes[uIndex == uLast ? uLast : uIndex+1];

	const bool		bPrvAccount		= isSetBit(pnPrv.uFlags, STPathElement::typeAccount);
	const bool		bNxtAccount		= isSetBit(pnNxt.uFlags, STPathElement::typeAccount);

	const uint160&	uCurAccountID	= pnCur.uAccountID;
	const uint160&	uPrvAccountID	= bPrvAccount ? pnPrv.uAccountID : uCurAccountID;
	const uint160&	uNxtAccountID	= bNxtAccount ? pnNxt.uAccountID : uCurAccountID;	// Offers are always issue.

	const uint160&	uCurrencyID		= pnCur.uCurrencyID;

	uint32			uQualityIn		= uIndex ? lesActive.rippleQualityIn(uCurAccountID, uPrvAccountID, uCurrencyID) : QUALITY_ONE;
	uint32			uQualityOut		= uIndex == uLast ? lesActive.rippleQualityOut(uCurAccountID, uNxtAccountID, uCurrencyID) : QUALITY_ONE;

	// For bNxtAccount
	const STAmount&	saPrvRedeemReq	= pnPrv.saFwdRedeem;
	STAmount		saPrvRedeemAct(saPrvRedeemReq.getCurrency(), saPrvRedeemReq.getIssuer());

	const STAmount&	saPrvIssueReq	= pnPrv.saFwdIssue;
	STAmount		saPrvIssueAct(saPrvIssueReq.getCurrency(), saPrvIssueReq.getIssuer());

	// For !bPrvAccount
	const STAmount&	saPrvDeliverReq	= pnPrv.saRevDeliver;
	STAmount		saPrvDeliverAct(saPrvDeliverReq.getCurrency(), saPrvDeliverReq.getIssuer());

	// For bNxtAccount
	const STAmount&	saCurRedeemReq	= pnCur.saRevRedeem;
	STAmount&		saCurRedeemAct	= pnCur.saFwdRedeem;

	const STAmount&	saCurIssueReq	= pnCur.saRevIssue;
	STAmount&		saCurIssueAct	= pnCur.saFwdIssue;

	// For !bNxtAccount
	const STAmount&	saCurDeliverReq	= pnCur.saRevDeliver;
	STAmount&		saCurDeliverAct	= pnCur.saFwdDeliver;

	cLog(lsINFO) << boost::str(boost::format("calcNodeAccountFwd> uIndex=%d/%d saPrvRedeemReq=%s saPrvIssueReq=%s saPrvDeliverReq=%s saCurRedeemReq=%s saCurIssueReq=%s saCurDeliverReq=%s")
		% uIndex
		% uLast
		% saPrvRedeemReq.getFullText()
		% saPrvIssueReq.getFullText()
		% saPrvDeliverReq.getFullText()
		% saCurRedeemReq.getFullText()
		% saCurIssueReq.getFullText()
		% saCurDeliverReq.getFullText());

	// Ripple through account.

	if (bPrvAccount && bNxtAccount)
	{
		if (!uIndex)
		{
			// ^ --> ACCOUNT --> account

			// First node, calculate amount to send.
			// XXX Limit by stamp/ripple balance

			const STAmount&	saCurSendMaxReq		= pspCur->saInReq.isNegative()
													? pspCur->saInReq	// Negative for no limit, doing a calculation.
													: pspCur->saInReq-pspCur->saInAct;	// request - done.
			STAmount&		saCurSendMaxPass	= pspCur->saInPass;		// Report how much pass sends.

			if (saCurRedeemReq)
			{
				// Redeem requested.
				saCurRedeemAct	= saCurRedeemReq.isNegative()
									? saCurRedeemReq
									: std::min(saCurRedeemReq, saCurSendMaxReq);
			}
			else
			{
				// No redeeming.

				saCurRedeemAct	= saCurRedeemReq;
			}
			saCurSendMaxPass	= saCurRedeemAct;

			if (saCurIssueReq && (saCurSendMaxReq.isNegative() || saCurSendMaxPass != saCurSendMaxReq))
			{
				// Issue requested and pass does not meet max.
				saCurIssueAct	= saCurSendMaxReq.isNegative()
									? saCurIssueReq
									: std::min(saCurSendMaxReq-saCurRedeemAct, saCurIssueReq);
			}
			else
			{
				// No issuing.

				saCurIssueAct	= STAmount(saCurIssueReq);
			}
			saCurSendMaxPass	+= saCurIssueAct;

			cLog(lsINFO) << boost::str(boost::format("calcNodeAccountFwd: ^ --> ACCOUNT --> account : saInReq=%s saInAct=%s saCurSendMaxReq=%s saCurRedeemAct=%s saCurIssueReq=%s saCurIssueAct=%s saCurSendMaxPass=%s")
				% pspCur->saInReq.getFullText()
				% pspCur->saInAct.getFullText()
				% saCurSendMaxReq.getFullText()
				% saCurRedeemAct.getFullText()
				% saCurIssueReq.getFullText()
				% saCurIssueAct.getFullText()
				% saCurSendMaxPass.getFullText());
		}
		else if (uIndex == uLast)
		{
			// account --> ACCOUNT --> $
			cLog(lsINFO) << boost::str(boost::format("calcNodeAccountFwd: account --> ACCOUNT --> $ : uPrvAccountID=%s uCurAccountID=%s saPrvRedeemReq=%s saPrvIssueReq=%s")
				% NewcoinAddress::createHumanAccountID(uPrvAccountID)
				% NewcoinAddress::createHumanAccountID(uCurAccountID)
				% saPrvRedeemReq.getFullText()
				% saPrvIssueReq.getFullText());

			// Last node.  Accept all funds.  Calculate amount actually to credit.

			STAmount&	saCurReceive	= pspCur->saOutPass;

			STAmount	saIssueCrd		= uQualityIn >= QUALITY_ONE
											? saPrvIssueReq													// No fee.
											: STAmount::multiply(saPrvIssueReq, uQualityIn, uCurrencyID, saPrvIssueReq.getIssuer());	// Fee.

			// Amount to credit.
			saCurReceive	= saPrvRedeemReq+saIssueCrd;

			// Actually receive.
			lesActive.rippleCredit(uPrvAccountID, uCurAccountID, saPrvRedeemReq+saPrvIssueReq, false);
		}
		else
		{
			// account --> ACCOUNT --> account
			cLog(lsINFO) << boost::str(boost::format("calcNodeAccountFwd: account --> ACCOUNT --> account"));

			// Previous redeem part 1: redeem -> redeem
			if (saPrvRedeemReq != saPrvRedeemAct)			// Previous wants to redeem. To next must be ok.
			{
				// Rate : 1.0 : quality out
				calcNodeRipple(QUALITY_ONE, uQualityOut, saPrvRedeemReq, saCurRedeemReq, saPrvRedeemAct, saCurRedeemAct, uRateMax);
			}

			// Previous issue part 1: issue -> redeem
			if (saPrvIssueReq != saPrvIssueAct				// Previous wants to issue.
				&& saCurRedeemReq != saCurRedeemAct)		// Current has more to redeem to next.
			{
				// Rate: quality in : quality out
				calcNodeRipple(uQualityIn, uQualityOut, saPrvIssueReq, saCurRedeemReq, saPrvIssueAct, saCurRedeemAct, uRateMax);
			}

			// Previous redeem part 2: redeem -> issue.
			// wants to redeem and current would and can issue.
			// If redeeming cur to next is done, this implies can issue.
			if (saPrvRedeemReq != saPrvRedeemAct			// Previous still wants to redeem.
				&& saCurRedeemReq == saCurRedeemAct			// Current has no more to redeem to next.
				&& saCurIssueReq)
			{
				// Rate : 1.0 : transfer_rate
				calcNodeRipple(QUALITY_ONE, lesActive.rippleTransferRate(uCurAccountID), saPrvRedeemReq, saCurIssueReq, saPrvRedeemAct, saCurIssueAct, uRateMax);
			}

			// Previous issue part 2 : issue -> issue
			if (saPrvIssueReq != saPrvIssueAct)				// Previous wants to issue. To next must be ok.
			{
				// Rate: quality in : 1.0
				calcNodeRipple(uQualityIn, QUALITY_ONE, saPrvIssueReq, saCurIssueReq, saPrvIssueAct, saCurIssueAct, uRateMax);
			}

			// Adjust prv --> cur balance : take all inbound
			// XXX Currency must be in amount.
			lesActive.rippleCredit(uPrvAccountID, uCurAccountID, saPrvRedeemReq + saPrvIssueReq, false);
		}
	}
	else if (bPrvAccount && !bNxtAccount)
	{
		// account --> ACCOUNT --> offer
		cLog(lsINFO) << boost::str(boost::format("calcNodeAccountFwd: account --> ACCOUNT --> offer"));

		// redeem -> issue.
		// wants to redeem and current would and can issue.
		// If redeeming cur to next is done, this implies can issue.
		if (saPrvRedeemReq)								// Previous wants to redeem.
		{
			// Rate : 1.0 : transfer_rate
			calcNodeRipple(QUALITY_ONE, lesActive.rippleTransferRate(uCurAccountID), saPrvRedeemReq, saCurDeliverReq, saPrvRedeemAct, saCurDeliverAct, uRateMax);
		}

		// issue -> issue
		if (saPrvRedeemReq == saPrvRedeemAct			// Previous done redeeming: Previous has no IOUs.
			&& saPrvIssueReq)							// Previous wants to issue. To next must be ok.
		{
			// Rate: quality in : 1.0
			calcNodeRipple(uQualityIn, QUALITY_ONE, saPrvIssueReq, saCurDeliverReq, saPrvIssueAct, saCurDeliverAct, uRateMax);
		}

		// Adjust prv --> cur balance : take all inbound
		// XXX Currency must be in amount.
		lesActive.rippleCredit(uPrvAccountID, uCurAccountID, saPrvRedeemReq + saPrvIssueReq, false);
	}
	else if (!bPrvAccount && bNxtAccount)
	{
		if (uIndex == uLast)
		{
			// offer --> ACCOUNT --> $
			cLog(lsINFO) << boost::str(boost::format("calcNodeAccountFwd: offer --> ACCOUNT --> $ : %s") % saPrvDeliverReq.getFullText());

			STAmount&	saCurReceive	= pspCur->saOutPass;

			// Amount to credit.
			saCurReceive	= saPrvDeliverReq;

			// No income balance adjustments necessary.  The paying side inside the offer paid to this account.
		}
		else
		{
			// offer --> ACCOUNT --> account
			cLog(lsINFO) << boost::str(boost::format("calcNodeAccountFwd: offer --> ACCOUNT --> account"));

			// deliver -> redeem
			if (saPrvDeliverReq)							// Previous wants to deliver.
			{
				// Rate : 1.0 : quality out
				calcNodeRipple(QUALITY_ONE, uQualityOut, saPrvDeliverReq, saCurRedeemReq, saPrvDeliverAct, saCurRedeemAct, uRateMax);
			}

			// deliver -> issue
			// Wants to redeem and current would and can issue.
			if (saPrvDeliverReq != saPrvDeliverAct			// Previous still wants to deliver.
				&& saCurRedeemReq == saCurRedeemAct			// Current has more to redeem to next.
				&& saCurIssueReq)							// Current wants issue.
			{
				// Rate : 1.0 : transfer_rate
				calcNodeRipple(QUALITY_ONE, lesActive.rippleTransferRate(uCurAccountID), saPrvDeliverReq, saCurIssueReq, saPrvDeliverAct, saCurIssueAct, uRateMax);
			}

			// No income balance adjustments necessary.  The paying side inside the offer paid and the next link will receive.
		}
	}
	else
	{
		// offer --> ACCOUNT --> offer
		// deliver/redeem -> deliver/issue.
		cLog(lsINFO) << boost::str(boost::format("calcNodeAccountFwd: offer --> ACCOUNT --> offer"));

		if (saPrvDeliverReq									// Previous wants to deliver
			&& saCurIssueReq)								// Current wants issue.
		{
			// Rate : 1.0 : transfer_rate
			calcNodeRipple(QUALITY_ONE, lesActive.rippleTransferRate(uCurAccountID), saPrvDeliverReq, saCurDeliverReq, saPrvDeliverAct, saCurDeliverAct, uRateMax);
		}

		// No income balance adjustments necessary.  The paying side inside the offer paid and the next link will receive.
	}

	return terResult;
}

// Return true, iff lhs has less priority than rhs.
bool PathState::lessPriority(const PathState::pointer& lhs, const PathState::pointer& rhs)
{
	if (lhs->uQuality != rhs->uQuality)
		return lhs->uQuality > rhs->uQuality;	// Bigger is worse.

	// Best quanity is second rank.
	if (lhs->saOutPass != rhs->saOutPass)
		return lhs->saOutPass < rhs->saOutPass;	// Smaller is worse.

	// Path index is third rank.
	return lhs->mIndex > rhs->mIndex;			// Bigger is worse.
}

// Make sure the path delivers to uAccountID: uCurrencyID from uIssuerID.
//
// Rules:
// - Currencies must be converted via an offer.
// - A node names it's output.
// - A ripple nodes output issuer must be the node's account or the next node's account.
// - Offers can only go directly to another offer if the currency and issuer are an exact match.
TER PathState::pushImply(
	const uint160& uAccountID,	// --> Delivering to this account.
	const uint160& uCurrencyID,	// --> Delivering this currency.
	const uint160& uIssuerID)	// --> Delivering this issuer.
{
	const PaymentNode&	pnPrv		= vpnNodes.back();
	TER					terResult	= tesSUCCESS;

	cLog(lsINFO) << "pushImply> "
		<< NewcoinAddress::createHumanAccountID(uAccountID)
		<< " " << STAmount::createHumanCurrency(uCurrencyID)
		<< " " << NewcoinAddress::createHumanAccountID(uIssuerID);

	if (pnPrv.uCurrencyID != uCurrencyID)
	{
		// Currency is different, need to convert via an offer.

		terResult	= pushNode(
					STPathElement::typeCurrency		// Offer.
					 | STPathElement::typeIssuer,
					ACCOUNT_ONE,	// Placeholder for offers.
					uCurrencyID,	// The offer's output is what is now wanted.
					uIssuerID);

	}

	// For ripple, non-stamps, ensure the issuer is on at least one side of the transaction.
	if (tesSUCCESS == terResult
		&& !!uCurrencyID							// Not stamps.
		&& (pnPrv.uAccountID != uIssuerID			// Previous is not issuing own IOUs.
			&& uAccountID != uIssuerID))			// Current is not receiving own IOUs.
	{
		// Need to ripple through uIssuerID's account.

		terResult	= pushNode(
					STPathElement::typeAccount,
					uIssuerID,						// Intermediate account is the needed issuer.
					uCurrencyID,
					uIssuerID);
	}

	cLog(lsINFO) << "pushImply< " << terResult;

	return terResult;
}

// Append a node and insert before it any implied nodes.
// <-- terResult: tesSUCCESS, temBAD_PATH, terNO_LINE
TER PathState::pushNode(
	const int iType,
	const uint160& uAccountID,
	const uint160& uCurrencyID,
	const uint160& uIssuerID)
{
	cLog(lsINFO) << "pushNode> "
		<< NewcoinAddress::createHumanAccountID(uAccountID)
		<< " " << STAmount::createHumanCurrency(uCurrencyID)
		<< "/" << NewcoinAddress::createHumanAccountID(uIssuerID);
	PaymentNode			pnCur;
	const bool			bFirst		= vpnNodes.empty();
	const PaymentNode&	pnPrv		= bFirst ? PaymentNode() : vpnNodes.back();
	// true, iff node is a ripple account. false, iff node is an offer node.
	const bool			bAccount	= isSetBit(iType, STPathElement::typeAccount);
	// true, iff currency supplied.
	// Currency is specified for the output of the current node.
	const bool			bCurrency	= isSetBit(iType, STPathElement::typeCurrency);
	// Issuer is specified for the output of the current node.
	const bool			bIssuer		= isSetBit(iType, STPathElement::typeIssuer);
	TER					terResult	= tesSUCCESS;

	pnCur.uFlags		= iType;

	if (iType & ~STPathElement::typeValidBits)
	{
		cLog(lsINFO) << "pushNode: bad bits.";

		terResult	= temBAD_PATH;
	}
	else if (bAccount)
	{
		// Account link

		pnCur.uAccountID	= uAccountID;
		pnCur.uCurrencyID	= bCurrency ? uCurrencyID : pnPrv.uCurrencyID;
		pnCur.uIssuerID		= bIssuer ? uIssuerID : uAccountID;
		pnCur.saRevRedeem	= STAmount(uCurrencyID, uAccountID);
		pnCur.saRevIssue	= STAmount(uCurrencyID, uAccountID);

		if (!bFirst)
		{
			// Add required intermediate nodes to deliver to current account.
			terResult	= pushImply(
				pnCur.uAccountID,									// Current account.
				pnCur.uCurrencyID,									// Wanted currency.
				!!pnCur.uCurrencyID ? uAccountID : ACCOUNT_XNS);	// Account as issuer.
		}

		if (tesSUCCESS == terResult && !vpnNodes.empty())
		{
			const PaymentNode&	pnBck		= vpnNodes.back();
			bool				bBckAccount	= isSetBit(pnBck.uFlags, STPathElement::typeAccount);

			if (bBckAccount)
			{
				SLE::pointer	sleRippleState	= mLedger->getSLE(Ledger::getRippleStateIndex(pnBck.uAccountID, pnCur.uAccountID, pnPrv.uCurrencyID));

				if (!sleRippleState)
				{
					cLog(lsINFO) << "pushNode: No credit line between "
						<< NewcoinAddress::createHumanAccountID(pnBck.uAccountID)
						<< " and "
						<< NewcoinAddress::createHumanAccountID(pnCur.uAccountID)
						<< " for "
						<< STAmount::createHumanCurrency(pnPrv.uCurrencyID)
						<< "." ;

					cLog(lsINFO) << getJson();

					terResult	= terNO_LINE;
				}
				else
				{
					cLog(lsINFO) << "pushNode: Credit line found between "
						<< NewcoinAddress::createHumanAccountID(pnBck.uAccountID)
						<< " and "
						<< NewcoinAddress::createHumanAccountID(pnCur.uAccountID)
						<< " for "
						<< STAmount::createHumanCurrency(pnPrv.uCurrencyID)
						<< "." ;
				}
			}
		}

		if (tesSUCCESS == terResult)
			vpnNodes.push_back(pnCur);
	}
	else
	{
		// Offer link
		// Offers bridge a change in currency & issuer or just a change in issuer.
		pnCur.uCurrencyID	= bCurrency ? uCurrencyID : pnPrv.uCurrencyID;
		pnCur.uIssuerID		= bIssuer ? uIssuerID : pnCur.uAccountID;
		pnCur.saRateMax		= saZero;

		if (!!pnPrv.uAccountID)
		{
			// Previous is an account.

			// Insert intermediary issuer account if needed.
			terResult	= pushImply(
				!!pnPrv.uCurrencyID
					? ACCOUNT_ONE	// Rippling, but offer's don't have an account.
					: ACCOUNT_XNS,
				pnPrv.uCurrencyID,
				pnPrv.uIssuerID);
		}

		if (tesSUCCESS == terResult)
		{
			vpnNodes.push_back(pnCur);
		}
	}
	cLog(lsINFO) << "pushNode< " << terResult;

	return terResult;
}

PathState::PathState(
	const int				iIndex,
	const LedgerEntrySet&	lesSource,
	const STPath&			spSourcePath,
	const uint160&			uReceiverID,
	const uint160&			uSenderID,
	const STAmount&			saSend,
	const STAmount&			saSendMax
	)
	: mLedger(lesSource.getLedgerRef()),
		mIndex(iIndex),
		uQuality(1),				// Mark path as active.
		saInReq(saSendMax),
		saOutReq(saSend)
{
	const uint160	uInCurrencyID	= saSendMax.getCurrency();
	const uint160	uOutCurrencyID	= saSend.getCurrency();
	const uint160	uInIssuerID		= !!uInCurrencyID ? saSendMax.getIssuer() : ACCOUNT_XNS;
	const uint160	uOutIssuerID	= !!uOutCurrencyID ? saSend.getIssuer() : ACCOUNT_XNS;

	lesEntries				= lesSource.duplicate();

	// Push sending node.
	terStatus	= pushNode(
		STPathElement::typeAccount
			| STPathElement::typeCurrency
			| STPathElement::typeIssuer,
		uSenderID,
		uInCurrencyID,
		uInIssuerID);

	BOOST_FOREACH(const STPathElement& speElement, spSourcePath)
	{
		if (tesSUCCESS == terStatus)
			terStatus	= pushNode(speElement.getNodeType(), speElement.getAccountID(), speElement.getCurrency(), speElement.getIssuerID());
	}

	if (tesSUCCESS == terStatus)
	{
		// Create receiver node.

		terStatus	= pushImply(uReceiverID, uOutCurrencyID, uOutIssuerID);
		if (tesSUCCESS == terStatus)
		{
			terStatus	= pushNode(
				STPathElement::typeAccount						// Last node is always an account.
					| STPathElement::typeCurrency
					| STPathElement::typeIssuer,
				uReceiverID,									// Receive to output
				uOutCurrencyID,									// Desired currency
				uOutIssuerID);
		}
	}

	if (tesSUCCESS == terStatus)
	{
		// Look for first mention of source in nodes and detect loops.
		// Note: The output is not allowed to be a source.

		const unsigned int	uNodes	= vpnNodes.size();

		for (unsigned int uIndex = 0; tesSUCCESS == terStatus && uIndex != uNodes; ++uIndex)
		{
			const PaymentNode&	pnCur	= vpnNodes[uIndex];

			if (!!pnCur.uAccountID)
			{
				// Source is a ripple line
				nothing();
			}
			else if (!umForward.insert(std::make_pair(boost::make_tuple(pnCur.uAccountID, pnCur.uCurrencyID, pnCur.uIssuerID), uIndex)).second)
			{
				// Failed to insert. Have a loop.
				cLog(lsINFO) << boost::str(boost::format("PathState: loop detected: %s")
					% getJson());

				terStatus	= temBAD_PATH_LOOP;
			}
		}
	}

	cLog(lsINFO) << boost::str(boost::format("PathState: in=%s/%s out=%s/%s %s")
		% STAmount::createHumanCurrency(uInCurrencyID)
		% NewcoinAddress::createHumanAccountID(uInIssuerID)
		% STAmount::createHumanCurrency(uOutCurrencyID)
		% NewcoinAddress::createHumanAccountID(uOutIssuerID)
		% getJson());
}

Json::Value	PathState::getJson() const
{
	Json::Value	jvPathState(Json::objectValue);
	Json::Value	jvNodes(Json::arrayValue);

	BOOST_FOREACH(const PaymentNode& pnNode, vpnNodes)
	{
		Json::Value	jvNode(Json::objectValue);

		Json::Value	jvFlags(Json::arrayValue);

		if (pnNode.uFlags & STPathElement::typeAccount)
			jvFlags.append("account");

		jvNode["flags"]	= jvFlags;

		if (pnNode.uFlags & STPathElement::typeAccount)
			jvNode["account"]	= NewcoinAddress::createHumanAccountID(pnNode.uAccountID);

		if (!!pnNode.uCurrencyID)
			jvNode["currency"]	= STAmount::createHumanCurrency(pnNode.uCurrencyID);

		if (!!pnNode.uIssuerID)
			jvNode["issuer"]	= NewcoinAddress::createHumanAccountID(pnNode.uIssuerID);

		// if (pnNode.saRevRedeem)
			jvNode["rev_redeem"]	= pnNode.saRevRedeem.getFullText();

		// if (pnNode.saRevIssue)
			jvNode["rev_issue"]		= pnNode.saRevIssue.getFullText();

		// if (pnNode.saRevDeliver)
			jvNode["rev_deliver"]	= pnNode.saRevDeliver.getFullText();

		// if (pnNode.saFwdRedeem)
			jvNode["fwd_redeem"]	= pnNode.saFwdRedeem.getFullText();

		// if (pnNode.saFwdIssue)
			jvNode["fwd_issue"]		= pnNode.saFwdIssue.getFullText();

		// if (pnNode.saFwdDeliver)
			jvNode["fwd_deliver"]	= pnNode.saFwdDeliver.getFullText();

		jvNodes.append(jvNode);
	}

	jvPathState["status"]	= terStatus;
	jvPathState["index"]	= mIndex;
	jvPathState["nodes"]	= jvNodes;

	if (saInReq)
		jvPathState["in_req"]	= saInReq.getJson(0);

	if (saInAct)
		jvPathState["in_act"]	= saInAct.getJson(0);

	if (saInPass)
		jvPathState["in_pass"]	= saInPass.getJson(0);

	if (saOutReq)
		jvPathState["out_req"]	= saOutReq.getJson(0);

	if (saOutAct)
		jvPathState["out_act"]	= saOutAct.getJson(0);

	if (saOutPass)
		jvPathState["out_pass"]	= saOutPass.getJson(0);

	if (uQuality)
		jvPathState["uQuality"]	= Json::Value::UInt(uQuality);

	return jvPathState;
}

TER RippleCalc::calcNodeFwd(const unsigned int uIndex, const PathState::pointer& pspCur, const bool bMultiQuality)
{
	const PaymentNode&		pnCur		= pspCur->vpnNodes[uIndex];
	const bool				bCurAccount	= isSetBit(pnCur.uFlags,  STPathElement::typeAccount);

	cLog(lsINFO) << boost::str(boost::format("calcNodeFwd> uIndex=%d") % uIndex);

	TER						terResult	= bCurAccount
											? calcNodeAccountFwd(uIndex, pspCur, bMultiQuality)
											: calcNodeOfferFwd(uIndex, pspCur, bMultiQuality);

	if (tesSUCCESS == terResult && uIndex + 1 != pspCur->vpnNodes.size())
	{
		terResult	= calcNodeFwd(uIndex+1, pspCur, bMultiQuality);
	}

	cLog(lsINFO) << boost::str(boost::format("calcNodeFwd< uIndex=%d terResult=%d") % uIndex % terResult);

	return terResult;
}

// Calculate a node and its previous nodes.
// From the destination work in reverse towards the source calculating how much must be asked for.
// Then work forward, figuring out how much can actually be delivered.
// <-- terResult: tesSUCCESS or tepPATH_DRY
// <-> pnNodes:
//     --> [end]saWanted.mAmount
//     --> [all]saWanted.mCurrency
//     --> [all]saAccount
//     <-> [0]saWanted.mAmount : --> limit, <-- actual
TER RippleCalc::calcNodeRev(const unsigned int uIndex, const PathState::pointer& pspCur, const bool bMultiQuality)
{
	PaymentNode&	pnCur		= pspCur->vpnNodes[uIndex];
	const bool		bCurAccount	= isSetBit(pnCur.uFlags,  STPathElement::typeAccount);
	TER				terResult;

	// Do current node reverse.
	const uint160&	uCurIssuerID	= pnCur.uIssuerID;
	STAmount&		saTransferRate	= pnCur.saTransferRate;

		saTransferRate	= STAmount::saFromRate(lesActive.rippleTransferRate(uCurIssuerID));

	cLog(lsINFO) << boost::str(boost::format("calcNodeRev> uIndex=%d uIssuerID=%s saTransferRate=%s")
		% uIndex
		% NewcoinAddress::createHumanAccountID(uCurIssuerID)
		% saTransferRate.getFullText());

	terResult	= bCurAccount
					? calcNodeAccountRev(uIndex, pspCur, bMultiQuality)
					: calcNodeOfferRev(uIndex, pspCur, bMultiQuality);

	// Do previous.
	if (tesSUCCESS != terResult)
	{
		// Error, don't continue.
		nothing();
	}
	else if (uIndex)
	{
		// Continue in reverse.

		terResult	= calcNodeRev(uIndex-1, pspCur, bMultiQuality);
	}

	cLog(lsINFO) << boost::str(boost::format("calcNodeRev< uIndex=%d terResult=%s/%d") % uIndex % transToken(terResult) % terResult);

	return terResult;
}

// Calculate the next increment of a path.
// The increment is what can satisfy a portion or all of the requested output at the best quality.
// <-- pspCur->uQuality
void RippleCalc::pathNext(const PathState::pointer& pspCur, const int iPaths, const LedgerEntrySet& lesCheckpoint, LedgerEntrySet& lesCurrent)
{
	// The next state is what is available in preference order.
	// This is calculated when referenced accounts changed.
	const bool			bMultiQuality	= iPaths == 1;
	const unsigned int	uLast			= pspCur->vpnNodes.size() - 1;

	// YYY This clearing should only be needed for nice logging.
	pspCur->saInPass	= STAmount(pspCur->saInReq.getCurrency(), pspCur->saInReq.getIssuer());
	pspCur->saOutPass	= STAmount(pspCur->saOutReq.getCurrency(), pspCur->saOutReq.getIssuer());

	pspCur->vUnfundedBecame.clear();
	pspCur->umReverse.clear();

	cLog(lsINFO) << "Path In: " << pspCur->getJson();

	assert(pspCur->vpnNodes.size() >= 2);

	lesCurrent	= lesCheckpoint;					// Restore from checkpoint.
	lesCurrent.bumpSeq();							// Begin ledger varance.

	pspCur->terStatus	= calcNodeRev(uLast, pspCur, bMultiQuality);

	cLog(lsINFO) << "Path after reverse: " << pspCur->getJson();

	if (tesSUCCESS == pspCur->terStatus)
	{
		// Do forward.
		lesCurrent	= lesCheckpoint;				// Restore from checkpoint.
		lesCurrent.bumpSeq();						// Begin ledger varance.

		pspCur->terStatus	= calcNodeFwd(0, pspCur, bMultiQuality);

		tLog(tesSUCCESS == pspCur->terStatus, lsDEBUG)
			<< boost::str(boost::format("saOutPass=%s saInPass=%s")
				% pspCur->saOutPass.getFullText()
				% pspCur->saInPass.getFullText());

		// Make sure we have a quality.
		assert(tesSUCCESS != pspCur->terStatus || (!!pspCur->saOutPass && !!pspCur->saInPass));

		pspCur->uQuality	= tesSUCCESS == pspCur->terStatus
								? STAmount::getRate(pspCur->saOutPass, pspCur->saInPass)	// Calculate relative quality.
								: 0;														// Mark path as inactive.

		cLog(lsINFO) << "Path after forward: " << pspCur->getJson();
	}
}

// XXX Stand alone calculation not implemented, does not calculate required input.
TER RippleCalc::rippleCalc(
	LedgerEntrySet&		lesActive,				// <-> --> = Fee applied to src balance.
		  STAmount&		saMaxAmountAct,			// <-- The computed input amount.
		  STAmount&		saDstAmountAct,			// <-- The computed output amount.
	const STAmount&		saMaxAmountReq,			// --> -1 = no limit.
	const STAmount&		saDstAmountReq,
	const uint160&		uDstAccountID,
	const uint160&		uSrcAccountID,
	const STPathSet&	spsPaths,
    const bool			bPartialPayment,
    const bool			bLimitQuality,
    const bool			bNoRippleDirect
    )
{
	RippleCalc		rc(lesActive);

    TER	    terResult	= temUNCERTAIN;

	// YYY Might do basic checks on src and dst validity as per doPayment.

    if (bNoRippleDirect && spsPaths.isEmpty())
    {
	    cLog(lsINFO) << "doPayment: Invalid transaction: No paths and direct ripple not allowed.";

	    return temRIPPLE_EMPTY;
    }

    // Incrementally search paths.
    std::vector<PathState::pointer>	vpsPaths;

    if (!bNoRippleDirect)
    {
	    // Direct path.
	    // XXX Might also make a stamp bridge by default.
	    cLog(lsINFO) << "doPayment: Build direct:";

	    PathState::pointer	pspDirect	= PathState::createPathState(
		    vpsPaths.size(),
		    lesActive,
		    STPath(),
		    uDstAccountID,
		    uSrcAccountID,
		    saDstAmountReq,
		    saMaxAmountReq);

	    if (pspDirect)
	    {
		    // Return if malformed.
		    if (pspDirect->terStatus >= temMALFORMED && pspDirect->terStatus < tefFAILURE)
			    return pspDirect->terStatus;

		    if (tesSUCCESS == pspDirect->terStatus)
		    {
			    // Had a success.
			    terResult	= tesSUCCESS;

			    vpsPaths.push_back(pspDirect);
		    }
	    }
    }

    cLog(lsINFO) << "doPayment: Paths in set: " << spsPaths.getPathCount();

    BOOST_FOREACH(const STPath& spPath, spsPaths)
    {
	    cLog(lsINFO) << "doPayment: Build path:";

	    PathState::pointer	pspExpanded	= PathState::createPathState(
		    vpsPaths.size(),
		    lesActive,
		    spPath,
		    uDstAccountID,
		    uSrcAccountID,
		    saDstAmountReq,
		    saMaxAmountReq);

	    if (pspExpanded)
	    {
		    // Return if malformed.
		    if (pspExpanded->terStatus >= temMALFORMED && pspExpanded->terStatus < tefFAILURE)
			    return pspExpanded->terStatus;

		    if (tesSUCCESS == pspExpanded->terStatus)
		    {
			    // Had a success.
			    terResult	= tesSUCCESS;
		    }

		    vpsPaths.push_back(pspExpanded);
	    }
    }

    if (vpsPaths.empty())
    {
	    return tefEXCEPTION;
    }
    else if (tesSUCCESS != terResult)
    {
	    // No path successes.

	    return vpsPaths[0]->terStatus;
    }
    else
    {
	    terResult	= temUNCERTAIN;
    }

	STAmount				saInAct;
	STAmount				saOutAct;
    const LedgerEntrySet	lesBase			= lesActive;							// Checkpoint with just fees paid.
    const uint64			uQualityLimit	= bLimitQuality ? STAmount::getRate(saDstAmountReq, saMaxAmountReq) : 0;
	// When processing, don't want to complicate directory walking with deletion.
	std::vector<uint256>	vuUnfundedBecame;										// Offers that became unfunded.

    while (temUNCERTAIN == terResult)
    {
	    PathState::pointer		pspBest;
	    const LedgerEntrySet	lesCheckpoint	= lesActive;

	    // Find the best path.
	    BOOST_FOREACH(PathState::pointer& pspCur, vpsPaths)
	    {
		    if (pspCur->uQuality)
			{
				pspCur->saInAct		= saInAct;										// Update to current amount processed.
				pspCur->saOutAct	= saOutAct;

				rc.pathNext(pspCur, vpsPaths.size(), lesCheckpoint, lesActive);		// Compute increment.

				if (!pspCur->uQuality) {
					// Path was dry.

					nothing();
				}
				else if ((!bLimitQuality || pspCur->uQuality <= uQualityLimit)		// Quality is not limted or increment has allowed quality.
					|| !pspBest														// Best is not yet set.
					|| PathState::lessPriority(pspBest, pspCur))					// Current is better than set.
				{
					lesActive.swapWith(pspCur->lesEntries);							// For the path, save ledger state.
					pspBest	= pspCur;
				}
			}
	    }

	    if (pspBest)
	    {
		    // Apply best path.

		    // Record best pass' offers that became unfunded for deletion on success.
		    vuUnfundedBecame.insert(vuUnfundedBecame.end(), pspBest->vUnfundedBecame.begin(), pspBest->vUnfundedBecame.end());

		    // Record best pass' LedgerEntrySet to build off of and potentially return.
		    lesActive.swapWith(pspBest->lesEntries);

			saInAct		+= pspBest->saInPass;
			saOutAct	+= pspBest->saOutPass;

		    if (temUNCERTAIN == terResult && saOutAct == saDstAmountReq)
		    {
				// Done. Delivered requested amount.

			    terResult	= tesSUCCESS;
		    }
		    else if (saInAct != saMaxAmountReq)
		    {
			    // Have not met requested amount or max send. Prepare for next pass.

			    // Merge best pass' umReverse.
			    rc.mumSource.insert(pspBest->umReverse.begin(), pspBest->umReverse.end());
		    }
			else if (!bPartialPayment)
			{
				// Have sent maximum allowed. Partial payment not allowed.

				terResult	= tepPATH_PARTIAL;
				lesActive	= lesBase;				// Revert to just fees charged.
			}
			else
			{
				// Have sent maximum allowed. Partial payment allowed.  Success.

				terResult	= tesSUCCESS;
			}
	    }
	    // Not done and ran out of paths.
	    else if (!bPartialPayment)
	    {
		    // Partial payment not allowed.
		    terResult	= tepPATH_PARTIAL;
		    lesActive	= lesBase;				// Revert to just fees charged.
	    }
	    // Partial payment ok.
	    else if (!saOutAct)
	    {
		    // No payment at all.
		    terResult	= tepPATH_DRY;
		    lesActive	= lesBase;				// Revert to just fees charged.
	    }
	    else
	    {
		    terResult	= tesSUCCESS;
	    }
    }

    if (tesSUCCESS == terResult)
    {
	    // Delete became unfunded offers.
	    BOOST_FOREACH(const uint256& uOfferIndex, vuUnfundedBecame)
	    {
		    if (tesSUCCESS == terResult)
			    terResult = lesActive.offerDelete(uOfferIndex);
	    }
    }

    // Delete found unfunded offers.
    BOOST_FOREACH(const uint256& uOfferIndex, rc.musUnfundedFound)
    {
	    if (tesSUCCESS == terResult)
		    terResult = lesActive.offerDelete(uOfferIndex);
    }

	return terResult;
}

#if 0
// XXX Need to adjust for fees.
// Find offers to satisfy pnDst.
// - Does not adjust any balances as there is at least a forward pass to come.
// --> pnDst.saWanted: currency and amount wanted
// --> pnSrc.saIOURedeem.mCurrency: use this before saIOUIssue, limit to use.
// --> pnSrc.saIOUIssue.mCurrency: use this after saIOURedeem, limit to use.
// <-- pnDst.saReceive
// <-- pnDst.saIOUForgive
// <-- pnDst.saIOUAccept
// <-- terResult : tesSUCCESS = no error and if !bAllowPartial complelely satisfied wanted.
// <-> usOffersDeleteAlways:
// <-> usOffersDeleteOnSuccess:
TER calcOfferFill(PaymentNode& pnSrc, PaymentNode& pnDst, bool bAllowPartial)
{
	TER	terResult;

	if (pnDst.saWanted.isNative())
	{
		// Transfer stamps.

		STAmount	saSrcFunds	= pnSrc.saAccount->accountHolds(pnSrc.saAccount, uint160(0), uint160(0));

		if (saSrcFunds && (bAllowPartial || saSrcFunds > pnDst.saWanted))
		{
			pnSrc.saSend	= min(saSrcFunds, pnDst.saWanted);
			pnDst.saReceive	= pnSrc.saSend;
		}
		else
		{
			terResult	= terINSUF_PATH;
		}
	}
	else
	{
		// Ripple funds.

		// Redeem to limit.
		terResult	= calcOfferFill(
			accountHolds(pnSrc.saAccount, pnDst.saWanted.getCurrency(), pnDst.saWanted.getIssuer()),
			pnSrc.saIOURedeem,
			pnDst.saIOUForgive,
			bAllowPartial);

		if (tesSUCCESS == terResult)
		{
			// Issue to wanted.
			terResult	= calcOfferFill(
				pnDst.saWanted,		// As much as wanted is available, limited by credit limit.
				pnSrc.saIOUIssue,
				pnDst.saIOUAccept,
				bAllowPartial);
		}

		if (tesSUCCESS == terResult && !bAllowPartial)
		{
			STAmount	saTotal	= pnDst.saIOUForgive	+ pnSrc.saIOUAccept;

			if (saTotal != saWanted)
				terResult	= terINSUF_PATH;
		}
	}

	return terResult;
}
#endif

#if 0
// Get the next offer limited by funding.
// - Stop when becomes unfunded.
void TransactionEngine::calcOfferBridgeNext(
	const uint256&		uBookRoot,		// --> Which order book to look in.
	const uint256&		uBookEnd,		// --> Limit of how far to look.
	uint256&			uBookDirIndex,	// <-> Current directory. <-- 0 = no offer available.
	uint64&				uBookDirNode,	// <-> Which node. 0 = first.
	unsigned int&		uBookDirEntry,	// <-> Entry in node. 0 = first.
	STAmount&			saOfferIn,		// <-- How much to pay in, fee inclusive, to get saOfferOut out.
	STAmount&			saOfferOut		// <-- How much offer pays out.
	)
{
	saOfferIn		= 0;	// XXX currency & issuer
	saOfferOut		= 0;	// XXX currency & issuer

	bool			bDone	= false;

	while (!bDone)
	{
		uint256			uOfferIndex;

		// Get uOfferIndex.
		mNodes.dirNext(uBookRoot, uBookEnd, uBookDirIndex, uBookDirNode, uBookDirEntry, uOfferIndex);

		SLE::pointer	sleOffer		= entryCache(ltOFFER, uOfferIndex);

		uint160			uOfferOwnerID	= sleOffer->getFieldAccount(sfAccount).getAccountID();
		STAmount		saOfferPays		= sleOffer->getFieldAmount(sfTakerGets);
		STAmount		saOfferGets		= sleOffer->getFieldAmount(sfTakerPays);

		if (sleOffer->isFieldPresent(sfExpiration) && sleOffer->getFieldU32(sfExpiration) <= mLedger->getParentCloseTimeNC())
		{
			// Offer is expired.
			cLog(lsINFO) << "calcOfferFirst: encountered expired offer";
		}
		else
		{
			STAmount		saOfferFunds	= accountFunds(uOfferOwnerID, saOfferPays);
			// Outbound fees are paid by offer owner.
			// XXX Calculate outbound fee rate.

			if (saOfferPays.isNative())
			{
				// No additional fees for stamps.

				nothing();
			}
			else if (saOfferPays.getIssuer() == uOfferOwnerID)
			{
				// Offerer is issue own IOUs.
				// No fees at this exact point, XXX receiving node may charge a fee.
				// XXX Make sure has a credit line with receiver, limit by credit line.

				nothing();
				// XXX Broken - could be issuing or redeeming or both.
			}
			else
			{
				// Offer must be redeeming IOUs.

				// No additional 
				// XXX Broken
			}

			if (!saOfferFunds.isPositive())
			{
				// Offer is unfunded.
				cLog(lsINFO) << "calcOfferFirst: offer unfunded: delete";
			}
			else if (saOfferFunds >= saOfferPays)
			{
				// Offer fully funded.

				// Account transfering funds in to offer always pays inbound fees.

				saOfferIn	= saOfferGets;	// XXX Add in fees?

				saOfferOut	= saOfferPays;

				bDone		= true;
			}
			else
			{
				// Offer partially funded.

				// saOfferIn/saOfferFunds = saOfferGets/saOfferPays
				// XXX Round such that all saOffer funds are exhausted.
				saOfferIn	= (saOfferFunds*saOfferGets)/saOfferPays; // XXX Add in fees?
				saOfferOut	= saOfferFunds;

				bDone		= true;
			}
		}

		if (!bDone)
		{
			// musUnfundedFound.insert(uOfferIndex);
		}
	}
	while (bNext);
}
#endif

#if 0
// If either currency is not stamps, then also calculates vs stamp bridge.
// --> saWanted: Limit of how much is wanted out.
// <-- saPay: How much to pay into the offer.
// <-- saGot: How much to the offer pays out.  Never more than saWanted.
// Given two value's enforce a minimum:
// - reverse: prv is maximum to pay in (including fee) - cur is what is wanted: generally, minimizing prv
// - forward: prv is actual amount to pay in (including fee) - cur is what is wanted: generally, minimizing cur
// Value in is may be rippled or credited from limbo. Value out is put in limbo.
// If next is an offer, the amount needed is in cur reedem.
// XXX What about account mentioned multiple times via offers?
void TransactionEngine::calcNodeOffer(
	bool			bForward,
	bool			bMultiQuality,	// True, if this is the only active path: we can do multiple qualities in this pass.
	const uint160&	uPrvAccountID,	// If 0, then funds from previous offer's limbo
	const uint160&	uPrvCurrencyID,
	const uint160&	uPrvIssuerID,
	const uint160&	uCurCurrencyID,
	const uint160&	uCurIssuerID,

	const STAmount& uPrvRedeemReq,	// --> In limit.
	STAmount&		uPrvRedeemAct,	// <-> In limit achived.
	const STAmount& uCurRedeemReq,	// --> Out limit. Driver when uCurIssuerID == uNxtIssuerID (offer would redeem to next)
	STAmount&		uCurRedeemAct,	// <-> Out limit achived.

	const STAmount& uCurIssueReq,	// --> In limit.
	STAmount&		uCurIssueAct,	// <-> In limit achived.
	const STAmount& uCurIssueReq,	// --> Out limit. Driver when uCurIssueReq != uNxtIssuerID (offer would effectively issue or transfer to next)
	STAmount&		uCurIssueAct,	// <-> Out limit achived.

	STAmount& saPay,
	STAmount& saGot
	) const
{
	TER	terResult	= temUNKNOWN;

	// Direct: not bridging via XNS
	bool			bDirectNext	= true;		// True, if need to load.
	uint256			uDirectQuality;
	uint256			uDirectTip	= Ledger::getBookBase(uGetsCurrency, uGetsIssuerID, uPaysCurrency, uPaysIssuerID);
	uint256			uDirectEnd	= Ledger::getQualityNext(uDirectTip);

	// Bridging: bridging via XNS
	bool			bBridge		= true;		// True, if bridging active. False, missing an offer.
	uint256			uBridgeQuality;
	STAmount		saBridgeIn;				// Amount available.
	STAmount		saBridgeOut;

	bool			bInNext		= true;		// True, if need to load.
	STAmount		saInIn;					// Amount available. Consumed in loop. Limited by offer funding.
	STAmount		saInOut;
	uint256			uInTip;					// Current entry.
	uint256			uInEnd;
	unsigned int	uInEntry;

	bool			bOutNext	= true;
	STAmount		saOutIn;
	STAmount		saOutOut;
	uint256			uOutTip;
	uint256			uOutEnd;
	unsigned int	uOutEntry;

	saPay.zero();
	saPay.setCurrency(uPrvCurrencyID);
	saPay.setIssuer(uPrvIssuerID);

	saNeed	= saWanted;

	if (!uCurCurrencyID && !uPrvCurrencyID)
	{
		// Bridging: Neither currency is XNS.
		uInTip		= Ledger::getBookBase(uPrvCurrencyID, uPrvIssuerID, CURRENCY_XNS, ACCOUNT_XNS);
		uInEnd		= Ledger::getQualityNext(uInTip);
		uOutTip		= Ledger::getBookBase(CURRENCY_XNS, ACCOUNT_XNS, uCurCurrencyID, uCurIssuerID);
		uOutEnd		= Ledger::getQualityNext(uInTip);
	}

	// Find our head offer.

	bool		bRedeeming		= false;
	bool		bIssuing		= false;

	// The price varies as we change between issuing and transfering, so unless bMultiQuality, we must stick with a mode once it
	// is determined.

	if (bBridge && (bInNext || bOutNext))
	{
		// Bridging and need to calculate next bridge rate.
		// A bridge can consist of multiple offers. As offer's are consumed, the effective rate changes.

		if (bInNext)
		{
//					sleInDir	= entryCache(ltDIR_NODE, mLedger->getNextLedgerIndex(uInIndex, uInEnd));
			// Get the next funded offer.
			offerBridgeNext(uInIndex, uInEnd, uInEntry, saInIn, saInOut);	// Get offer limited by funding.
			bInNext		= false;
		}

		if (bOutNext)
		{
//					sleOutDir	= entryCache(ltDIR_NODE, mLedger->getNextLedgerIndex(uOutIndex, uOutEnd));
			offerNext(uOutIndex, uOutEnd, uOutEntry, saOutIn, saOutOut);
			bOutNext	= false;
		}

		if (!uInIndex || !uOutIndex)
		{
			bBridge	= false;	// No more offers to bridge.
		}
		else
		{
			// Have bridge in and out entries.
			// Calculate bridge rate.  Out offer pay ripple fee.  In offer fee is added to in cost.

			saBridgeOut.zero();

			if (saInOut < saOutIn)
			{
				// Limit by in.

				// XXX Need to include fees in saBridgeIn.
				saBridgeIn	= saInIn;	// All of in
				// Limit bridge out: saInOut/saBridgeOut = saOutIn/saOutOut
				// Round such that we would take all of in offer, otherwise would have leftovers.
				saBridgeOut	= (saInOut * saOutOut) / saOutIn;
			}
			else if (saInOut > saOutIn)
			{
				// Limit by out, if at all.

				// XXX Need to include fees in saBridgeIn.
				// Limit bridge in:saInIn/saInOuts = aBridgeIn/saOutIn
				// Round such that would take all of out offer.
				saBridgeIn	= (saInIn * saOutIn) / saInOuts;
				saBridgeOut	= saOutOut;		// All of out.
			}
			else
			{
				// Entries match,

				// XXX Need to include fees in saBridgeIn.
				saBridgeIn	= saInIn;	// All of in
				saBridgeOut	= saOutOut;	// All of out.
			}

			uBridgeQuality	= STAmount::getRate(saBridgeIn, saBridgeOut);	// Inclusive of fees.
		}
	}

	if (bBridge)
	{
		bUseBridge	= !uDirectTip || (uBridgeQuality < uDirectQuality)
	}
	else if (!!uDirectTip)
	{
		bUseBridge	= false
	}
	else
	{
		// No more offers. Declare success, even if none returned.
		saGot		= saWanted-saNeed;
		terResult	= tesSUCCESS;
	}

	if (tesSUCCESS != terResult)
	{
		STAmount&	saAvailIn	= bUseBridge ? saBridgeIn : saDirectIn;
		STAmount&	saAvailOut	= bUseBridge ? saBridgeOut : saDirectOut;

		if (saAvailOut > saNeed)
		{
			// Consume part of offer. Done.

			saNeed	= 0;
			saPay	+= (saNeed*saAvailIn)/saAvailOut; // Round up, prefer to pay more.
		}
		else
		{
			// Consume entire offer.

			saNeed	-= saAvailOut;
			saPay	+= saAvailIn;

			if (bUseBridge)
			{
				// Consume bridge out.
				if (saOutOut == saAvailOut)
				{
					// Consume all.
					saOutOut	= 0;
					saOutIn		= 0;
					bOutNext	= true;
				}
				else
				{
					// Consume portion of bridge out, must be consuming all of bridge in.
					// saOutIn/saOutOut = saSpent/saAvailOut
					// Round?
					saOutIn		-= (saOutIn*saAvailOut)/saOutOut;
					saOutOut	-= saAvailOut;
				}

				// Consume bridge in.
				if (saOutIn == saAvailIn)
				{
					// Consume all.
					saInOut		= 0;
					saInIn		= 0;
					bInNext		= true;
				}
				else
				{
					// Consume portion of bridge in, must be consuming all of bridge out.
					// saInIn/saInOut = saAvailIn/saPay
					// Round?
					saInOut	-= (saInOut*saAvailIn)/saInIn;
					saInIn	-= saAvailIn;
				}
			}
			else
			{
				bDirectNext	= true;
			}
		}
	}
}
#endif

// vim:ts=4
