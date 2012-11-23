// YYY OPTIMIZE: When calculating path increment, note if increment consumes all liquidity. No need to revesit path in the future
// if all liquidity is used.

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
	const unsigned int			uNode,				// 0 < uNode < uLast
	PathState::ref				pspCur,
	const bool					bMultiQuality,
	const bool					bReverse)
{
	PaymentNode&	pnPrv			= pspCur->vpnNodes[uNode-1];
	PaymentNode&	pnCur			= pspCur->vpnNodes[uNode];

	const uint160&	uPrvCurrencyID	= pnPrv.uCurrencyID;
	const uint160&	uPrvIssuerID	= pnPrv.uIssuerID;
	const uint160&	uCurCurrencyID	= pnCur.uCurrencyID;
	const uint160&	uCurIssuerID	= pnCur.uIssuerID;

	uint256&		uDirectTip		= pnCur.uDirectTip;
	uint256&		uDirectEnd		= pnCur.uDirectEnd;
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

		if (!uDirectTip)
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
				cLog(lsWARNING) << "calcNodeAdvance: Unreachable: Fell off end of order book.";
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
				cLog(lsWARNING) << boost::str(boost::format("calcNodeAdvance: unreachable: ran out of offers"));
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

			cLog(lsINFO) << boost::str(boost::format("calcNodeAdvance: uOfrOwnerID=%s") % RippleAddress::createHumanAccountID(uOfrOwnerID));

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
			// XXX Going forward could we fund something with a worse quality which was previously skipped? Might need to check
			//     quality.
			curIssuerNodeConstIterator	itForward		= pspCur->umForward.find(asLine);
			const bool					bFoundForward	= itForward != pspCur->umForward.end();

			if (bFoundForward && itForward->second != uNode)
			{
				// Temporarily unfunded. Another node uses this source, ignore in this offer.
				cLog(lsINFO) << "calcNodeAdvance: temporarily unfunded offer (forward)";

				bEntryAdvance	= true;
				continue;
			}

			curIssuerNodeConstIterator	itPast			= mumSource.find(asLine);
			bool						bFoundPast		= itPast != mumSource.end();

			if (bFoundPast && itPast->second != uNode)
			{
				// Temporarily unfunded. Another node uses this source, ignore in this offer.
				cLog(lsINFO) << "calcNodeAdvance: temporarily unfunded offer (past)";

				bEntryAdvance	= true;
				continue;
			}

			curIssuerNodeConstIterator	itReverse		= pspCur->umReverse.find(asLine);
			bool						bFoundReverse	= itReverse != pspCur->umReverse.end();

			if (bFoundReverse && itReverse->second != uNode)
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
					% RippleAddress::createHumanAccountID(uOfrOwnerID)
					% STAmount::createHumanCurrency(uCurCurrencyID)
					% RippleAddress::createHumanAccountID(uCurIssuerID));

				pspCur->umReverse.insert(std::make_pair(asLine, uNode));
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

// Between offer nodes, the fee charged may vary.  Therefore, process one inbound offer at a time.  Propagate the inbound offer's
// requirements to the previous node.  The previous node adjusts the amount output and the amount spent on fees.  Continue process
// till request is satisified while we the rate does not increase past the initial rate.
TER RippleCalc::calcNodeDeliverRev(
	const unsigned int			uNode,			// 0 < uNode < uLast
	PathState::ref				pspCur,
	const bool					bMultiQuality,
	const uint160&				uOutAccountID,	// --> Output owner's account.
	const STAmount&				saOutReq,		// --> Funds wanted.
	STAmount&					saOutAct)		// <-- Funds delivered.
{
	TER	terResult	= tesSUCCESS;

	PaymentNode&	pnPrv			= pspCur->vpnNodes[uNode-1];
	PaymentNode&	pnCur			= pspCur->vpnNodes[uNode];

	const uint160&	uCurIssuerID	= pnCur.uIssuerID;
	const uint160&	uPrvAccountID	= pnPrv.uAccountID;
	const STAmount&	saTransferRate	= pnCur.saTransferRate;

	STAmount&		saPrvDlvReq		= pnPrv.saRevDeliver;	// To be set.
	STAmount&		saCurDlvFwd		= pnCur.saFwdDeliver;


	uint256&		uDirectTip		= pnCur.uDirectTip;

	uDirectTip		= 0;									// Restart book searching.

	saCurDlvFwd.zero(saOutReq);								// For forward pass zero deliver.

	saPrvDlvReq.zero(pnPrv.uCurrencyID, pnPrv.uIssuerID);
	saOutAct.zero(saOutReq);

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

		terResult	= calcNodeAdvance(uNode, pspCur, bMultiQuality, true);		// If needed, advance to next funded offer.

		if (tesSUCCESS != terResult || !uOfferIndex)
		{
			// Error or out of offers.
			break;
		}

		const STAmount	saOutFeeRate	= uOfrOwnerID == uCurIssuerID || uOutAccountID == uCurIssuerID // Issuer receiving or sending.
											? saOne				// No fee.
											: saTransferRate;	// Transfer rate of issuer.
		cLog(lsINFO) << boost::str(boost::format("calcNodeDeliverRev: uOfrOwnerID=%s uOutAccountID=%s uCurIssuerID=%s saTransferRate=%s saOutFeeRate=%s")
			% RippleAddress::createHumanAccountID(uOfrOwnerID)
			% RippleAddress::createHumanAccountID(uOutAccountID)
			% RippleAddress::createHumanAccountID(uCurIssuerID)
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
		else if (saOutFeeRate > saRateMax)
		{
			// Offer exceeds initial rate.
			cLog(lsINFO) << boost::str(boost::format("calcNodeDeliverRev: Offer exceeds initial rate: saRateMax=%s saOutFeeRate=%s")
				% saRateMax
				% saOutFeeRate);

			break;	// Done. Don't bother looking for smaller saTransferRates.
		}
		else if (saOutFeeRate < saRateMax)
		{
			// Reducing rate. Additional offers will only considered for this increment if they are at least this good.

			saRateMax	= saOutFeeRate;

			cLog(lsINFO) << boost::str(boost::format("calcNodeDeliverRev: Reducing rate: saRateMax=%s")
				% saRateMax);
		}

		// Amount that goes to the taker.
		STAmount	saOutPass		= std::min(std::min(saOfferFunds, saTakerGets), saOutReq-saOutAct);	// Offer maximum out - assuming no out fees.
		// Amount charged to the offer owner.
		// The fee goes to issuer. The fee is paid by offer owner and not passed as a cost to taker.
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

		// Compute portion of input needed to cover actual output.

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
			// Due to node expansion, previous is guaranteed to be the issuer.
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
			// Chain and compute the previous offer now.

			terResult	= calcNodeDeliverRev(
				uNode-1,
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

		// Want to deduct output to limit calculations while computing reverse.  Don't actually need to send.
		// Sending could be complicated: could fund a previous offer not yet visited.
		// However, these deductions and adjustments are tenative.
		// Must reset balances when going forward to perform actual transfers.
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

// For current offer, get input from deliver/limbo and output to next account or deliver for next offers.
// <-- pnCur.saFwdDeliver: For calcNodeAccountFwd to know how much went through
TER RippleCalc::calcNodeDeliverFwd(
	const unsigned int			uNode,			// 0 < uNode < uLast
	PathState::ref				pspCur,
	const bool					bMultiQuality,
	const uint160&				uInAccountID,	// --> Input owner's account.
	const STAmount&				saInReq,		// --> Amount to deliver.
	STAmount&					saInAct,		// <-- Amount delivered, this invokation.
	STAmount&					saInFees)		// <-- Fees charged, this invokation.
{
	TER	terResult	= tesSUCCESS;

	PaymentNode&	pnPrv			= pspCur->vpnNodes[uNode-1];
	PaymentNode&	pnCur			= pspCur->vpnNodes[uNode];
	PaymentNode&	pnNxt			= pspCur->vpnNodes[uNode+1];

	const uint160&	uNxtAccountID	= pnNxt.uAccountID;
	const uint160&	uCurCurrencyID	= pnCur.uCurrencyID;
	const uint160&	uCurIssuerID	= pnCur.uIssuerID;
	const uint160&	uPrvCurrencyID	= pnPrv.uCurrencyID;
	const uint160&	uPrvIssuerID	= pnPrv.uIssuerID;
	const STAmount&	saInTransRate	= pnPrv.saTransferRate;

	STAmount&		saCurDeliverAct	= pnCur.saFwdDeliver;	// Zeroed in reverse pass.

	uint256&		uDirectTip		= pnCur.uDirectTip;

	uDirectTip		= 0;						// Restart book searching.

	saInAct.zero(saInReq);
	saInFees.zero(saInReq);

	while (tesSUCCESS == terResult
		&& saInAct + saInFees != saInReq)		// Did not deliver all funds.
	{
		// Determine values for pass to adjust saInAct, saInFees, and saCurDeliverAct
		terResult	= calcNodeAdvance(uNode, pspCur, bMultiQuality, false);				// If needed, advance to next funded offer.

		if (tesSUCCESS == terResult)
		{
			// Doesn't charge input. Input funds are in limbo.
			bool&			bEntryAdvance	= pnCur.bEntryAdvance;
			STAmount&		saOfrRate		= pnCur.saOfrRate;
			uint256&		uOfferIndex		= pnCur.uOfferIndex;
			SLE::pointer&	sleOffer		= pnCur.sleOffer;
			const uint160&	uOfrOwnerID		= pnCur.uOfrOwnerID;
			bool&			bFundsDirty		= pnCur.bFundsDirty;
			STAmount&		saOfferFunds	= pnCur.saOfferFunds;
			STAmount&		saTakerPays		= pnCur.saTakerPays;
			STAmount&		saTakerGets		= pnCur.saTakerGets;

			const STAmount	saInFeeRate		= !uPrvCurrencyID					// XRP.
												|| uInAccountID == uPrvIssuerID	// Sender is issuer.
												|| uOfrOwnerID == uPrvIssuerID	// Reciever is issuer.
													? saOne						// No fee.
													: saInTransRate;			// Transfer rate of issuer.

			// First calculate assuming no output fees: saInPassAct, saInPassFees, saOutPassAct

			STAmount	saOutFunded		= std::min(saOfferFunds, saTakerGets);						// Offer maximum out - If there are no out fees.
			STAmount	saInFunded		= STAmount::multiply(saOutFunded, saOfrRate, saTakerPays);	// Offer maximum in - Limited by by payout.
			STAmount	saInTotal		= STAmount::multiply(saInFunded, saInTransRate);			// Offer maximum in with fees.
			STAmount	saInSum			= std::min(saInTotal, saInReq-saInAct-saInFees);			// In limited by remaining.
			STAmount	saInPassAct		= STAmount::divide(saInSum, saInFeeRate);					// In without fees.
			STAmount	saOutPassMax	= STAmount::divide(saInPassAct, saOfrRate, saTakerGets);	// Out limited by in remaining.

			STAmount	saInPassFeesMax	= saInSum-saInPassAct;

			STAmount	saOutPassAct;	// Will be determined by next node.
			STAmount	saInPassFees;	// Will be determined by adjusted saInPassAct.


			cLog(lsINFO) << boost::str(boost::format("calcNodeDeliverFwd: uNode=%d saOutFunded=%s saInReq=%s saInAct=%s saInFees=%s saInFunded=%s saInTotal=%s saInSum=%s saInPassAct=%s saOutPassMax=%s")
				% uNode
				% saOutFunded
				% saInReq
				% saInAct
				% saInFees
				% saInFunded
				% saInTotal
				% saInSum
				% saInPassAct
				% saOutPassMax);

			if (!!uNxtAccountID)
			{
				// ? --> OFFER --> account
				// Input fees: vary based upon the consumed offer's owner.
				// Output fees: none as XRP or the destination account is the issuer.

				saOutPassAct	= saOutPassMax;
				saInPassFees	= saInPassFeesMax;

				cLog(lsDEBUG) << boost::str(boost::format("calcNodeDeliverFwd: ? --> OFFER --> account: uOfrOwnerID=%s uNxtAccountID=%s saOutPassAct=%s")
					% RippleAddress::createHumanAccountID(uOfrOwnerID)
					% RippleAddress::createHumanAccountID(uNxtAccountID)
					% saOutPassAct.getFullText());

				// Output: Debit offer owner, send XRP or non-XPR to next account.
				lesActive.accountSend(uOfrOwnerID, uNxtAccountID, saOutPassAct);
			}
			else
			{
				// ? --> OFFER --> offer
				// Offer to offer means current order book's output currency and issuer match next order book's input current and
				// issuer.
				// Output fees: possible if issuer has fees and is not on either side.
				STAmount	saOutPassFees;

				// Output fees vary as the next nodes offer owners may vary.
				// Therefore, immediately push through output for current offer.
				terResult	= RippleCalc::calcNodeDeliverFwd(
					uNode+1,
					pspCur,
					bMultiQuality,
					uOfrOwnerID,		// --> Current holder.
					saOutPassMax,		// --> Amount available.
					saOutPassAct,		// <-- Amount delivered.
					saOutPassFees);		// <-- Fees charged.

				if (tesSUCCESS != terResult)
					break;

				if (saOutPassAct == saOutPassMax)
				{
					// No fees and entire output amount.

					saInPassFees	= saInPassFeesMax;
				}
				else
				{
					// Fraction of output amount.
					// Output fees are paid by offer owner and not passed to previous.
					saInPassAct		= STAmount::multiply(saOutPassAct, saOfrRate, saInReq);
					saInPassFees	= std::min(saInPassFeesMax, STAmount::multiply(saInPassAct, saInFeeRate));
				}

				// Do outbound debiting.
				// Send to issuer/limbo total amount including fees (issuer gets fees).
				lesActive.accountSend(uOfrOwnerID, !!uCurCurrencyID ? uCurIssuerID : ACCOUNT_XRP, saOutPassAct+saOutPassFees);

				cLog(lsINFO) << boost::str(boost::format("calcNodeDeliverFwd: ? --> OFFER --> offer: saOutPassAct=%s saOutPassFees=%s")
					% saOutPassAct
					% saOutPassFees);
			}

			cLog(lsINFO) << boost::str(boost::format("calcNodeDeliverFwd: uNode=%d saTakerGets=%s saTakerPays=%s saInPassAct=%s saInPassFees=%s saOutPassAct=%s saOutFunded=%s")
				% uNode
				% saTakerGets
				% saTakerPays
				% saInPassAct
				% saInPassFees
				% saOutPassAct
				% saOutFunded);

			// Funds were spent.
			bFundsDirty		= true;

			// Do inbound crediting.
			// Credit offer owner from in issuer/limbo (input transfer fees left with owner).
			lesActive.accountSend(!!uPrvCurrencyID ? uInAccountID : ACCOUNT_XRP, uOfrOwnerID, saInPassAct);

			// Adjust offer
			// Fees are considered paid from a seperate budget and are not named in the offer.
			sleOffer->setFieldAmount(sfTakerGets, saTakerGets - saOutPassAct);
			sleOffer->setFieldAmount(sfTakerPays, saTakerPays - saInPassAct);

			lesActive.entryModify(sleOffer);

			if (saOutPassAct == saOutFunded)
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

	cLog(lsDEBUG) << boost::str(boost::format("calcNodeDeliverFwd< uNode=%d saInAct=%s saInFees=%s")
		% uNode
		% saInAct
		% saInFees);

	return terResult;
}

// Called to drive from the last offer node in a chain.
TER RippleCalc::calcNodeOfferRev(
	const unsigned int			uNode,				// 0 < uNode < uLast
	PathState::ref				pspCur,
	const bool					bMultiQuality)
{
	TER				terResult;

	PaymentNode&	pnCur			= pspCur->vpnNodes[uNode];
	PaymentNode&	pnNxt			= pspCur->vpnNodes[uNode+1];

	if (!!pnNxt.uAccountID)
	{
		// Next is an account node, resolve current offer node's deliver.
		STAmount		saDeliverAct;

		terResult	= calcNodeDeliverRev(
							uNode,
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
// - Offer input is in issuer/limbo.
// - Current offers consumed.
//   - Current offer owners debited.
//   - Transfer fees credited to issuer.
//   - Payout to issuer or limbo.
// - Deliver is set without transfer fees.
TER RippleCalc::calcNodeOfferFwd(
	const unsigned int			uNode,				// 0 < uNode < uLast
	PathState::ref				pspCur,
	const bool					bMultiQuality
	)
{
	TER				terResult;
	PaymentNode&	pnPrv			= pspCur->vpnNodes[uNode-1];

	if (!!pnPrv.uAccountID)
	{
		// Previous is an account node, resolve its deliver.
		STAmount		saInAct;
		STAmount		saInFees;

		terResult	= calcNodeDeliverFwd(
							uNode,
							pspCur,
							bMultiQuality,
							pnPrv.uAccountID,
							pnPrv.saFwdDeliver,	// Previous is sending this much.
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

// Compute how much might flow for the node for the pass. Don not actually adjust balances.
// uQualityIn -> uQualityOut
//   saPrvReq -> saCurReq
//   sqPrvAct -> saCurAct
// This is a minimizing routine: moving in reverse it propagates the send limit to the sender, moving forward it propagates the
// actual send toward the receiver.
// This routine works backwards:
// - cur is the driver: it calculates previous wants based on previous credit limits and current wants.
// This routine works forwards:
// - prv is the driver: it calculates current deliver based on previous delivery limits and current wants.
// This routine is called one or two times for a node in a pass. If called once, it will work and set a rate.  If called again,
// the new work must not worsen the previous rate.
// XXX Deal with uQualityIn or uQualityOut = 0
void RippleCalc::calcNodeRipple(
	const uint32 uQualityIn,
	const uint32 uQualityOut,
	const STAmount& saPrvReq,	// --> in limit including fees, <0 = unlimited
	const STAmount& saCurReq,	// --> out limit (driver)
	STAmount& saPrvAct,			// <-> in limit including achieved so far: <-- <= -->
	STAmount& saCurAct,			// <-> out limit including achieved : <-- <= -->
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

		// Only process if we are not worsing previously processed.
		if (!uRateMax || STAmount::uRateOne <= uRateMax)
		{
			// Limit amount to transfer if need.
			STAmount	saTransfer	= bPrvUnlimited ? saCur : std::min(saPrv, saCur);

			// In reverse, we want to propagate the limited cur to prv and set actual cur.
			// In forward, we want to propagate the limited prv to cur and set actual prv.
			saPrvAct	+= saTransfer;
			saCurAct	+= saTransfer;

			// If no rate limit, set rate limit to avoid combining with something with a worse rate.
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
// Based on required deliverable, propagate redeem, issue, and deliver requests to the previous node.
// Inflate amount requested by required fees.
// Reedems are limited based on IOUs previous has on hand.
// Issues are limited based on credit limits and amount owed.
// No account balance adjustments as we don't know how much is going to actually be pushed through yet.
// <-- tesSUCCESS or tepPATH_DRY
TER RippleCalc::calcNodeAccountRev(const unsigned int uNode, PathState::ref pspCur, const bool bMultiQuality)
{
	TER					terResult		= tesSUCCESS;
	const unsigned int	uLast			= pspCur->vpnNodes.size() - 1;

	uint64				uRateMax		= 0;

	PaymentNode&		pnPrv			= pspCur->vpnNodes[uNode ? uNode-1 : 0];
	PaymentNode&		pnCur			= pspCur->vpnNodes[uNode];
	PaymentNode&		pnNxt			= pspCur->vpnNodes[uNode == uLast ? uLast : uNode+1];

	// Current is allowed to redeem to next.
	const bool			bPrvAccount		= !uNode || isSetBit(pnPrv.uFlags, STPathElement::typeAccount);
	const bool			bNxtAccount		= uNode == uLast || isSetBit(pnNxt.uFlags, STPathElement::typeAccount);

	const uint160&		uCurAccountID	= pnCur.uAccountID;
	const uint160&		uPrvAccountID	= bPrvAccount ? pnPrv.uAccountID : uCurAccountID;
	const uint160&		uNxtAccountID	= bNxtAccount ? pnNxt.uAccountID : uCurAccountID;	// Offers are always issue.

	const uint160&		uCurrencyID		= pnCur.uCurrencyID;

	// XXX Don't look up quality for XRP
	const uint32		uQualityIn		= uNode ? lesActive.rippleQualityIn(uCurAccountID, uPrvAccountID, uCurrencyID) : QUALITY_ONE;
	const uint32		uQualityOut		= uNode != uLast ? lesActive.rippleQualityOut(uCurAccountID, uNxtAccountID, uCurrencyID) : QUALITY_ONE;

	// For bPrvAccount
	const STAmount		saPrvOwed		= bPrvAccount && uNode								// Previous account is owed.
											? lesActive.rippleOwed(uCurAccountID, uPrvAccountID, uCurrencyID)
											: STAmount(uCurrencyID, uCurAccountID);

	const STAmount		saPrvLimit		= bPrvAccount && uNode								// Previous account may owe.
											? lesActive.rippleLimit(uCurAccountID, uPrvAccountID, uCurrencyID)
											: STAmount(uCurrencyID, uCurAccountID);

	const STAmount		saNxtOwed		= bNxtAccount && uNode != uLast					// Next account is owed.
											? lesActive.rippleOwed(uCurAccountID, uNxtAccountID, uCurrencyID)
											: STAmount(uCurrencyID, uCurAccountID);

	cLog(lsINFO) << boost::str(boost::format("calcNodeAccountRev> uNode=%d/%d uPrvAccountID=%s uCurAccountID=%s uNxtAccountID=%s uCurrencyID=%s uQualityIn=%d uQualityOut=%d saPrvOwed=%s saPrvLimit=%s")
		% uNode
		% uLast
		% RippleAddress::createHumanAccountID(uPrvAccountID)
		% RippleAddress::createHumanAccountID(uCurAccountID)
		% RippleAddress::createHumanAccountID(uNxtAccountID)
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

	if (!uNode)
	{
		// ^ --> ACCOUNT -->  account|offer
		// Nothing to do, there is no previous to adjust.

		nothing();
	}
	else if (bPrvAccount && bNxtAccount)
	{
		if (uNode == uLast)
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

				saCurWantedAct		= std::min(saPrvRedeemReq, saCurWantedReq);
				saPrvRedeemAct		= saCurWantedAct;

				uRateMax			= STAmount::uRateOne;

				cLog(lsINFO) << boost::str(boost::format("calcNodeAccountRev: Redeem at 1:1 saPrvRedeemReq=%s (available) saPrvRedeemAct=%s uRateMax=%s")
					% saPrvRedeemReq.getFullText()
					% saPrvRedeemAct.getFullText()
					% STAmount::saFromRate(uRateMax).getText());
			}
			else
			{
				saPrvRedeemAct.zero(saCurWantedAct);
			}

			// Calculate issuing.
			saPrvIssueAct.zero(saCurWantedAct);

			if (saCurWantedReq != saCurWantedAct		// Need more.
				&& saPrvIssueReq)						// Will accept IOUs from prevous.
			{
				// Rate: quality in : 1.0

				// If we previously redeemed and this has a poorer rate, this won't be included the current increment.
				calcNodeRipple(uQualityIn, QUALITY_ONE, saPrvIssueReq, saCurWantedReq, saPrvIssueAct, saCurWantedAct, uRateMax);

				cLog(lsINFO) << boost::str(boost::format("calcNodeAccountRev: Issuing: Rate: quality in : 1.0 saPrvIssueAct=%s saCurWantedAct=%s")
					% saPrvIssueAct.getFullText()
					% saCurWantedAct.getFullText());
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
			saPrvRedeemAct.zero(saCurRedeemReq);
			saPrvIssueAct.zero(saCurRedeemReq);

			// redeem (part 1) -> redeem
			if (saCurRedeemReq							// Next wants IOUs redeemed.
				&& saPrvRedeemReq)						// Previous has IOUs to redeem.
			{
				// Rate : 1.0 : quality out
				calcNodeRipple(QUALITY_ONE, uQualityOut, saPrvRedeemReq, saCurRedeemReq, saPrvRedeemAct, saCurRedeemAct, uRateMax);

				cLog(lsINFO) << boost::str(boost::format("calcNodeAccountRev: Rate : 1.0 : quality out saPrvRedeemAct=%s saCurRedeemAct=%s")
					% saPrvRedeemAct.getFullText()
					% saCurRedeemAct.getFullText());
			}

			// issue (part 1) -> redeem
			if (saCurRedeemReq != saCurRedeemAct		// Next wants more IOUs redeemed.
				&& saPrvRedeemAct == saPrvRedeemReq)	// Previous has no IOUs to redeem remaining.
			{
				// Rate: quality in : quality out
				calcNodeRipple(uQualityIn, uQualityOut, saPrvIssueReq, saCurRedeemReq, saPrvIssueAct, saCurRedeemAct, uRateMax);

				cLog(lsINFO) << boost::str(boost::format("calcNodeAccountRev: Rate: quality in : quality out: saPrvIssueAct=%s saCurRedeemAct=%s")
					% saPrvIssueAct.getFullText()
					% saCurRedeemAct.getFullText());
			}

			// redeem (part 2) -> issue.
			if (saCurIssueReq							// Next wants IOUs issued.
				&& saCurRedeemAct == saCurRedeemReq		// Can only issue if completed redeeming.
				&& saPrvRedeemAct != saPrvRedeemReq)	// Did not complete redeeming previous IOUs.
			{
				// Rate : 1.0 : transfer_rate
				calcNodeRipple(QUALITY_ONE, lesActive.rippleTransferRate(uCurAccountID), saPrvRedeemReq, saCurIssueReq, saPrvRedeemAct, saCurIssueAct, uRateMax);

				cLog(lsINFO) << boost::str(boost::format("calcNodeAccountRev: Rate : 1.0 : transfer_rate: saPrvRedeemAct=%s saCurIssueAct=%s")
					% saPrvRedeemAct.getFullText()
					% saCurIssueAct.getFullText());
			}

			// issue (part 2) -> issue
			if (saCurIssueReq != saCurIssueAct			// Need wants more IOUs issued.
				&& saCurRedeemAct == saCurRedeemReq		// Can only issue if completed redeeming.
				&& saPrvRedeemReq == saPrvRedeemAct		// Previously redeemed all owed IOUs.
				&& saPrvIssueReq)						// Previous can issue.
			{
				// Rate: quality in : 1.0
				calcNodeRipple(uQualityIn, QUALITY_ONE, saPrvIssueReq, saCurIssueReq, saPrvIssueAct, saCurIssueAct, uRateMax);

				cLog(lsINFO) << boost::str(boost::format("calcNodeAccountRev: Rate: quality in : 1.0: saPrvIssueAct=%s saCurIssueAct=%s")
					% saPrvIssueAct.getFullText()
					% saCurIssueAct.getFullText());
			}

			if (!saCurRedeemAct && !saCurIssueAct)
			{
				// Did not make progress.
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

		saPrvRedeemAct.zero(saCurRedeemReq);
		saPrvIssueAct.zero(saCurRedeemReq);

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
		saPrvDeliverAct.zero(saCurRedeemReq);

		if (uNode == uLast)
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

		saPrvDeliverAct.zero(saCurRedeemReq);

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

// The reverse pass has been narrowing by credit available and inflating by fees as it worked backwards.
// Now, for the current account node, take the actual amount from previous and adjust forward balances.
//
// Perform balance adjustments between previous and current node.
// - The previous node: specifies what to push through to current.
// - All of previous output is consumed.
// Then, compute current node's output for next node.
// - Current node: specify what to push through to next.
// - Output to next node is computed as input minus quality or transfer fee.
// - If next node is an offer and output is non-XRP then we are the issuer and do not need to push funds.
// - If next node is an offer and output is XRP then we need to deliver funds to limbo.
TER RippleCalc::calcNodeAccountFwd(
	const unsigned int			uNode,				// 0 <= uNode <= uLast
	PathState::ref				pspCur,
	const bool					bMultiQuality)
{
	TER					terResult		= tesSUCCESS;
	const unsigned int	uLast			= pspCur->vpnNodes.size() - 1;

	uint64				uRateMax		= 0;

	PaymentNode&	pnPrv			= pspCur->vpnNodes[uNode ? uNode-1 : 0];
	PaymentNode&	pnCur			= pspCur->vpnNodes[uNode];
	PaymentNode&	pnNxt			= pspCur->vpnNodes[uNode == uLast ? uLast : uNode+1];

	const bool		bPrvAccount		= isSetBit(pnPrv.uFlags, STPathElement::typeAccount);
	const bool		bNxtAccount		= isSetBit(pnNxt.uFlags, STPathElement::typeAccount);

	const uint160&	uCurAccountID	= pnCur.uAccountID;
	const uint160&	uPrvAccountID	= bPrvAccount ? pnPrv.uAccountID : uCurAccountID;
	const uint160&	uNxtAccountID	= bNxtAccount ? pnNxt.uAccountID : uCurAccountID;	// Offers are always issue.

//	const uint160&	uCurIssuerID	= pnCur.uIssuerID;

	const uint160&	uCurrencyID		= pnCur.uCurrencyID;

	uint32			uQualityIn		= uNode ? lesActive.rippleQualityIn(uCurAccountID, uPrvAccountID, uCurrencyID) : QUALITY_ONE;
	uint32			uQualityOut		= uNode == uLast ? lesActive.rippleQualityOut(uCurAccountID, uNxtAccountID, uCurrencyID) : QUALITY_ONE;

	// When looking backward (prv) for req we care about what we just calculated: use fwd
	// When looking forward (cur) for req we care about what was desired: use rev

	// For bNxtAccount
	const STAmount&	saPrvRedeemReq	= pnPrv.saFwdRedeem;
	STAmount		saPrvRedeemAct(saPrvRedeemReq.getCurrency(), saPrvRedeemReq.getIssuer());

	const STAmount&	saPrvIssueReq	= pnPrv.saFwdIssue;
	STAmount		saPrvIssueAct(saPrvIssueReq.getCurrency(), saPrvIssueReq.getIssuer());

	// For !bPrvAccount
	const STAmount&	saPrvDeliverReq	= pnPrv.saFwdDeliver;
	STAmount		saPrvDeliverAct(saPrvDeliverReq.getCurrency(), saPrvDeliverReq.getIssuer());

	// For bNxtAccount
	const STAmount&	saCurRedeemReq	= pnCur.saRevRedeem;
	STAmount&		saCurRedeemAct	= pnCur.saFwdRedeem;

	const STAmount&	saCurIssueReq	= pnCur.saRevIssue;
	STAmount&		saCurIssueAct	= pnCur.saFwdIssue;

	// For !bNxtAccount
	const STAmount&	saCurDeliverReq	= pnCur.saRevDeliver;
	STAmount&		saCurDeliverAct	= pnCur.saFwdDeliver;

	// For !uNode
	STAmount&		saCurSendMaxPass	= pspCur->saInPass;		// Report how much pass sends.

	cLog(lsINFO) << boost::str(boost::format("calcNodeAccountFwd> uNode=%d/%d saPrvRedeemReq=%s saPrvIssueReq=%s saPrvDeliverReq=%s saCurRedeemReq=%s saCurIssueReq=%s saCurDeliverReq=%s")
		% uNode
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
		// Next is an account, must be rippling.

		if (!uNode)
		{
			// ^ --> ACCOUNT --> account

			// First node, calculate amount to ripple based on what is available.

			saCurRedeemAct		= saCurRedeemReq;

			if (!pspCur->saInReq.isNegative())
			{
				// Limit by send max.
				saCurRedeemAct		= std::min(saCurRedeemAct, pspCur->saInReq-pspCur->saInAct);
			}

			saCurSendMaxPass	= saCurRedeemAct;

			saCurIssueAct		= saCurRedeemAct == saCurRedeemReq		// Fully redeemed.
									? saCurIssueReq
									: STAmount(saCurIssueReq);

			if (!!saCurIssueAct && !pspCur->saInReq.isNegative())
			{
				// Limit by send max.
				saCurIssueAct		= std::min(saCurIssueAct, pspCur->saInReq-pspCur->saInAct-saCurRedeemAct);
			}

			saCurSendMaxPass	+= saCurIssueAct;

			cLog(lsINFO) << boost::str(boost::format("calcNodeAccountFwd: ^ --> ACCOUNT --> account : saInReq=%s saInAct=%s saCurRedeemAct=%s saCurIssueReq=%s saCurIssueAct=%s saCurSendMaxPass=%s")
				% pspCur->saInReq.getFullText()
				% pspCur->saInAct.getFullText()
				% saCurRedeemAct.getFullText()
				% saCurIssueReq.getFullText()
				% saCurIssueAct.getFullText()
				% saCurSendMaxPass.getFullText());
		}
		else if (uNode == uLast)
		{
			// account --> ACCOUNT --> $
			cLog(lsINFO) << boost::str(boost::format("calcNodeAccountFwd: account --> ACCOUNT --> $ : uPrvAccountID=%s uCurAccountID=%s saPrvRedeemReq=%s saPrvIssueReq=%s")
				% RippleAddress::createHumanAccountID(uPrvAccountID)
				% RippleAddress::createHumanAccountID(uCurAccountID)
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

			saCurRedeemAct.zero(saCurRedeemReq);
			saCurIssueAct.zero(saCurIssueReq);

			// Previous redeem part 1: redeem -> redeem
			if (saPrvRedeemReq && saCurRedeemReq)			// Previous wants to redeem.
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
			if (saPrvRedeemReq != saPrvRedeemAct			// Previous still wants to redeem.
				&& saCurRedeemReq == saCurRedeemAct			// Current redeeming is done can issue.
				&& saCurIssueReq)							// Current wants to issue.
			{
				// Rate : 1.0 : transfer_rate
				calcNodeRipple(QUALITY_ONE, lesActive.rippleTransferRate(uCurAccountID), saPrvRedeemReq, saCurIssueReq, saPrvRedeemAct, saCurIssueAct, uRateMax);
			}

			// Previous issue part 2 : issue -> issue
			if (saPrvIssueReq != saPrvIssueAct				// Previous wants to issue.
				&& saCurRedeemReq == saCurRedeemAct)		// Current redeeming is done can issue.
			{
				// Rate: quality in : 1.0
				calcNodeRipple(uQualityIn, QUALITY_ONE, saPrvIssueReq, saCurIssueReq, saPrvIssueAct, saCurIssueAct, uRateMax);
			}

			// Adjust prv --> cur balance : take all inbound
			lesActive.rippleCredit(uPrvAccountID, uCurAccountID, saPrvRedeemReq + saPrvIssueReq, false);
		}
	}
	else if (bPrvAccount && !bNxtAccount)
	{
		// Current account is issuer to next offer.
		// Determine deliver to offer amount.
		// Don't adjust outbound balances- keep funds with issuer as limbo.
		// If issuer hold's an offer owners inbound IOUs, there is no fee and redeem/issue will transparently happen.

		if (uNode)
		{
			// Non-XRP, current node is the issuer.
			cLog(lsDEBUG) << boost::str(boost::format("calcNodeAccountFwd: account --> ACCOUNT --> offer"));

			saCurDeliverAct.zero(saCurDeliverReq);

			// redeem -> issue/deliver.
			// Previous wants to redeem.
			// Current is issuing to an offer so leave funds in account as "limbo".
			if (saPrvRedeemReq)								// Previous wants to redeem.
			{
				// Rate : 1.0 : transfer_rate
				// XXX Is having the transfer rate here correct?
				calcNodeRipple(QUALITY_ONE, lesActive.rippleTransferRate(uCurAccountID), saPrvRedeemReq, saCurDeliverReq, saPrvRedeemAct, saCurDeliverAct, uRateMax);
			}

			// issue -> issue/deliver
			if (saPrvRedeemReq == saPrvRedeemAct			// Previous done redeeming: Previous has no IOUs.
				&& saPrvIssueReq)							// Previous wants to issue. To next must be ok.
			{
				// Rate: quality in : 1.0
				calcNodeRipple(uQualityIn, QUALITY_ONE, saPrvIssueReq, saCurDeliverReq, saPrvIssueAct, saCurDeliverAct, uRateMax);
			}

			// Adjust prv --> cur balance : take all inbound
			lesActive.rippleCredit(uPrvAccountID, uCurAccountID, saPrvRedeemReq + saPrvIssueReq, false);
		}
		else
		{
			// Delivering amount requested from downstream.
			saCurDeliverAct		= saCurDeliverReq;

			// If limited, then limit by send max and available.
			if (!pspCur->saInReq.isNegative())
			{
				// Limit by send max.
				saCurDeliverAct		= std::min(saCurDeliverAct, pspCur->saInReq-pspCur->saInAct);

				// Limit XRP by available. No limit for non-XRP as issuer.
				if (!uCurAccountID)
					saCurDeliverAct	= std::min(saCurDeliverAct, lesActive.accountHolds(uCurAccountID, CURRENCY_XRP, ACCOUNT_XRP));

			}
			saCurSendMaxPass	= saCurDeliverAct;						// Record amount sent for pass.

			if (!!uCurrencyID)
			{
				// Non-XRP, current node is the issuer.
				// We could be delivering to multiple accounts, so we don't know which ripple balance will be adjusted.  Assume
				// just issuing.

				cLog(lsDEBUG) << boost::str(boost::format("calcNodeAccountFwd: ^ --> ACCOUNT -- !XRP --> offer"));

				// As the issuer, would only issue.
				// Don't need to actually deliver. As from delivering leave in the issuer as limbo.
				nothing();
			}
			else
			{
				cLog(lsDEBUG) << boost::str(boost::format("calcNodeAccountFwd: ^ --> ACCOUNT -- XRP --> offer"));

				// Deliver XRP to limbo.
				lesActive.accountSend(uCurAccountID, ACCOUNT_XRP, saCurDeliverAct);
			}
		}
	}
	else if (!bPrvAccount && bNxtAccount)
	{
		if (uNode == uLast)
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

			saCurRedeemAct.zero(saCurRedeemReq);
			saCurIssueAct.zero(saCurIssueReq);

			// deliver -> redeem
			if (saPrvDeliverReq && saCurRedeemReq)			// Previous wants to deliver and can current redeem.
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

		saCurDeliverAct.zero(saCurDeliverReq);

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
bool PathState::lessPriority(PathState::ref lhs, PathState::ref rhs)
{
	if (lhs->uQuality != rhs->uQuality)
		return lhs->uQuality > rhs->uQuality;	// Bigger is worse.

	// Best quanity is second rank.
	if (lhs->saOutPass != rhs->saOutPass)
		return lhs->saOutPass < rhs->saOutPass;	// Smaller is worse.

	// Path index is third rank.
	return lhs->mIndex > rhs->mIndex;			// Bigger is worse.
}

// Make sure last path node delivers to uAccountID: uCurrencyID from uIssuerID.
//
// If the unadded next node as specified by arguments would not work as is, then add the necessary nodes so it would work.
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
		<< RippleAddress::createHumanAccountID(uAccountID)
		<< " " << STAmount::createHumanCurrency(uCurrencyID)
		<< " " << RippleAddress::createHumanAccountID(uIssuerID);

	if (pnPrv.uCurrencyID != uCurrencyID)
	{
		// Currency is different, need to convert via an offer.

		terResult	= pushNode( // Offer.
						!!uCurrencyID
							? STPathElement::typeCurrency | STPathElement::typeIssuer
							: STPathElement::typeCurrency,
						ACCOUNT_XRP,					// Placeholder for offers.
						uCurrencyID,					// The offer's output is what is now wanted.
						uIssuerID);
	}

	const PaymentNode&	pnBck		= vpnNodes.back();

	// For ripple, non-XRP, ensure the issuer is on at least one side of the transaction.
	if (tesSUCCESS == terResult
		&& !!uCurrencyID								// Not XRP.
		&& (pnBck.uAccountID != uIssuerID				// Previous is not issuing own IOUs.
			&& uAccountID != uIssuerID))				// Current is not receiving own IOUs.
	{
		// Need to ripple through uIssuerID's account.

		terResult	= pushNode(
						STPathElement::typeAccount | STPathElement::typeCurrency | STPathElement::typeIssuer,
						uIssuerID,						// Intermediate account is the needed issuer.
						uCurrencyID,
						uIssuerID);
	}

	cLog(lsDEBUG) << boost::str(boost::format("pushImply< : %s") % transToken(terResult));

	return terResult;
}

// Append a node and insert before it any implied nodes.
// Offers may go back to back.
// <-- terResult: tesSUCCESS, temBAD_PATH, terNO_LINE, tepPATH_DRY
TER PathState::pushNode(
	const int iType,
	const uint160& uAccountID,
	const uint160& uCurrencyID,
	const uint160& uIssuerID)
{
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

	cLog(lsDEBUG) << "pushNode> "
		<< iType
		<< ": " << (bAccount ? RippleAddress::createHumanAccountID(uAccountID) : "-")
		<< " " << (bCurrency ? STAmount::createHumanCurrency(uCurrencyID) : "-")
		<< "/" << (bIssuer ? RippleAddress::createHumanAccountID(uIssuerID) : "-");

	pnCur.uFlags		= iType;
	pnCur.uCurrencyID	= bCurrency ? uCurrencyID : pnPrv.uCurrencyID;

	if (iType & ~STPathElement::typeValidBits)
	{
		cLog(lsDEBUG) << "pushNode: bad bits.";

		terResult	= temBAD_PATH;
	}
	else if (bIssuer && !pnCur.uCurrencyID)
	{
		cLog(lsDEBUG) << "pushNode: issuer specified for XRP.";

		terResult	= temBAD_PATH;
	}
	else if (bIssuer && !uIssuerID)
	{
		cLog(lsDEBUG) << "pushNode: specified bad issuer.";

		terResult	= temBAD_PATH;
	}
	else if (bAccount)
	{
		// Account link

		pnCur.uAccountID	= uAccountID;
		pnCur.uIssuerID		= bIssuer
									? uIssuerID
									: !!pnCur.uCurrencyID
										? uAccountID
										: ACCOUNT_XRP;
		pnCur.saRevRedeem	= STAmount(uCurrencyID, uAccountID);
		pnCur.saRevIssue	= STAmount(uCurrencyID, uAccountID);

		if (bFirst)
		{
			// The first node is always correct as is.

			nothing();
		}
		else if (!uAccountID)
		{
			cLog(lsDEBUG) << "pushNode: specified bad account.";

			terResult	= temBAD_PATH;
		}
		else
		{
			// Add required intermediate nodes to deliver to current account.
			terResult	= pushImply(
				pnCur.uAccountID,									// Current account.
				pnCur.uCurrencyID,									// Wanted currency.
				!!pnCur.uCurrencyID ? uAccountID : ACCOUNT_XRP);	// Account as wanted issuer.

			// Note: pnPrv may no longer be the immediately previous node.
		}

		if (tesSUCCESS == terResult && !vpnNodes.empty())
		{
			const PaymentNode&	pnBck		= vpnNodes.back();
			bool				bBckAccount	= isSetBit(pnBck.uFlags, STPathElement::typeAccount);

			if (bBckAccount)
			{
				SLE::pointer	sleRippleState	= lesEntries.entryCache(ltRIPPLE_STATE, Ledger::getRippleStateIndex(pnBck.uAccountID, pnCur.uAccountID, pnPrv.uCurrencyID));

				if (!sleRippleState)
				{
					cLog(lsINFO) << "pushNode: No credit line between "
						<< RippleAddress::createHumanAccountID(pnBck.uAccountID)
						<< " and "
						<< RippleAddress::createHumanAccountID(pnCur.uAccountID)
						<< " for "
						<< STAmount::createHumanCurrency(pnCur.uCurrencyID)
						<< "." ;

					cLog(lsINFO) << getJson();

					terResult	= terNO_LINE;
				}
				else
				{
					cLog(lsINFO) << "pushNode: Credit line found between "
						<< RippleAddress::createHumanAccountID(pnBck.uAccountID)
						<< " and "
						<< RippleAddress::createHumanAccountID(pnCur.uAccountID)
						<< " for "
						<< STAmount::createHumanCurrency(pnCur.uCurrencyID)
						<< "." ;

					STAmount	saOwed	= lesEntries.rippleOwed(pnCur.uAccountID, pnBck.uAccountID, pnCur.uCurrencyID);

					if (!saOwed.isPositive() && *saOwed.negate() >= lesEntries.rippleLimit(pnCur.uAccountID, pnBck.uAccountID, pnCur.uCurrencyID))
					{
						terResult	= tepPATH_DRY;
					}
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
		pnCur.uIssuerID		= bIssuer
									? uIssuerID
									: !!pnCur.uCurrencyID
										? !!pnPrv.uIssuerID
											? pnPrv.uIssuerID	// Default to previous issuer
											: pnPrv.uAccountID	// Or previous account if no previous issuer.
										: ACCOUNT_XRP;
		pnCur.saRateMax		= saZero;

		if (!!pnCur.uCurrencyID != !!pnCur.uIssuerID)
		{
			cLog(lsDEBUG) << "pushNode: currency is inconsistent with issuer.";

			terResult	= temBAD_PATH;
		}
		else if (!!pnPrv.uAccountID)
		{
			// Previous is an account.

			// Insert intermediary issuer account if needed.
			terResult	= pushImply(
				ACCOUNT_XRP,				// Rippling, but offer's don't have an account.
				pnPrv.uCurrencyID,
				pnPrv.uIssuerID);
		}

		if (tesSUCCESS == terResult)
		{
			vpnNodes.push_back(pnCur);
		}
	}
	cLog(lsDEBUG) << boost::str(boost::format("pushNode< : %s") % transToken(terResult));

	return terResult;
}

// terStatus = tesSUCCESS, temBAD_PATH, terNO_LINE, or temBAD_PATH_LOOP
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
	const uint160	uMaxCurrencyID	= saSendMax.getCurrency();
	const uint160	uMaxIssuerID	= saSendMax.getIssuer();

	const uint160	uOutCurrencyID	= saSend.getCurrency();
	const uint160	uOutIssuerID	= saSend.getIssuer();
	const uint160	uSenderIssuerID	= !!uMaxCurrencyID ? uSenderID : ACCOUNT_XRP;	// Sender is always issuer for non-XRP.

	lesEntries				= lesSource.duplicate();

	terStatus	= tesSUCCESS;

	if ((!uMaxCurrencyID && !!uMaxIssuerID) || (!uOutCurrencyID && !!uOutIssuerID))
		terStatus	= temBAD_PATH;

	// Push sending node.
	if (tesSUCCESS == terStatus)
		terStatus	= pushNode(
			!!uMaxCurrencyID
				? STPathElement::typeAccount | STPathElement::typeCurrency | STPathElement::typeIssuer
				: STPathElement::typeAccount | STPathElement::typeCurrency,
			uSenderID,
			uMaxCurrencyID,									// Max specifes the currency.
			uSenderIssuerID);

cLog(lsDEBUG) << boost::str(boost::format("PathState: pushed: account=%s currency=%s issuer=%s")
	% RippleAddress::createHumanAccountID(uSenderID)
	% STAmount::createHumanCurrency(uMaxCurrencyID)
	% RippleAddress::createHumanAccountID(uSenderIssuerID));

	if (tesSUCCESS == terStatus
		&& uMaxIssuerID != uSenderIssuerID) {				// Issuer was not same as sender
		// May have an implied node.

		// Figure out next node properties for implied node.
		const uint160	uNxtCurrencyID	= spSourcePath.size()
											? spSourcePath.getElement(0).getCurrency()
											: uOutCurrencyID;
		const uint160	uNxtAccountID	= spSourcePath.size()
											? spSourcePath.getElement(0).getAccountID()
											: !!uOutCurrencyID
												? uOutIssuerID == uReceiverID
													? uReceiverID
													: uOutIssuerID
												: ACCOUNT_XRP;

cLog(lsDEBUG) << boost::str(boost::format("PathState: implied check: uNxtCurrencyID=%s uNxtAccountID=%s")
	% RippleAddress::createHumanAccountID(uNxtCurrencyID)
	% RippleAddress::createHumanAccountID(uNxtAccountID));

		// Can't just use push implied, because it can't compensate for next account.
		if (!uNxtCurrencyID							// Next is XRP - will have offer next
				|| uMaxCurrencyID != uNxtCurrencyID	// Next is different current - will have offer next
				|| uMaxIssuerID != uNxtAccountID)	// Next is not implied issuer
		{
cLog(lsDEBUG) << boost::str(boost::format("PathState: sender implied: account=%s currency=%s issuer=%s")
	% RippleAddress::createHumanAccountID(uMaxIssuerID)
	% RippleAddress::createHumanAccountID(uMaxCurrencyID)
	% RippleAddress::createHumanAccountID(uMaxIssuerID));
			// Add account implied by SendMax.
			terStatus	= pushNode(
				!!uMaxCurrencyID
					? STPathElement::typeAccount | STPathElement::typeCurrency | STPathElement::typeIssuer
					: STPathElement::typeAccount | STPathElement::typeCurrency,
				uMaxIssuerID,
				uMaxCurrencyID,
				uMaxIssuerID);
		}
	}

	BOOST_FOREACH(const STPathElement& speElement, spSourcePath)
	{
		if (tesSUCCESS == terStatus)
			terStatus	= pushNode(speElement.getNodeType(), speElement.getAccountID(), speElement.getCurrency(), speElement.getIssuerID());
	}

	const PaymentNode&	pnPrv			= vpnNodes.back();

	if (tesSUCCESS == terStatus
		&& !!uOutCurrencyID							// Next is not XRP
		&& uOutIssuerID != uReceiverID				// Out issuer is not reciever
		&& (pnPrv.uCurrencyID != uOutCurrencyID		// Previous will be an offer.
			|| pnPrv.uAccountID != uOutIssuerID))	// Need the implied issuer.
	{
		// Add implied account.
cLog(lsDEBUG) << boost::str(boost::format("PathState: receiver implied: account=%s currency=%s issuer=%s")
	% RippleAddress::createHumanAccountID(uOutIssuerID)
	% RippleAddress::createHumanAccountID(uOutCurrencyID)
	% RippleAddress::createHumanAccountID(uOutIssuerID));
		terStatus	= pushNode(
			!!uOutCurrencyID
				? STPathElement::typeAccount | STPathElement::typeCurrency | STPathElement::typeIssuer
				: STPathElement::typeAccount | STPathElement::typeCurrency,
			uOutIssuerID,
			uOutCurrencyID,
			uOutIssuerID);
	}

	if (tesSUCCESS == terStatus)
	{
		// Create receiver node.
		// Last node is always an account.

		terStatus	= pushNode(
			!!uOutCurrencyID
				? STPathElement::typeAccount | STPathElement::typeCurrency | STPathElement::typeIssuer
				: STPathElement::typeAccount | STPathElement::typeCurrency,
			uReceiverID,									// Receive to output
			uOutCurrencyID,									// Desired currency
			uReceiverID);
	}

	if (tesSUCCESS == terStatus)
	{
		// Look for first mention of source in nodes and detect loops.
		// Note: The output is not allowed to be a source.

		const unsigned int	uNodes	= vpnNodes.size();

		for (unsigned int uNode = 0; tesSUCCESS == terStatus && uNode != uNodes; ++uNode)
		{
			const PaymentNode&	pnCur	= vpnNodes[uNode];

			if (!!pnCur.uAccountID)
			{
				// Source is a ripple line
				nothing();
			}
			else if (!umForward.insert(std::make_pair(boost::make_tuple(pnCur.uAccountID, pnCur.uCurrencyID, pnCur.uIssuerID), uNode)).second)
			{
				// Failed to insert. Have a loop.
				cLog(lsDEBUG) << boost::str(boost::format("PathState: loop detected: %s")
					% getJson());

				terStatus	= temBAD_PATH_LOOP;
			}
		}
	}

	cLog(lsINFO) << boost::str(boost::format("PathState: in=%s/%s out=%s/%s %s")
		% STAmount::createHumanCurrency(uMaxCurrencyID)
		% RippleAddress::createHumanAccountID(uMaxIssuerID)
		% STAmount::createHumanCurrency(uOutCurrencyID)
		% RippleAddress::createHumanAccountID(uOutIssuerID)
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
			jvNode["account"]	= RippleAddress::createHumanAccountID(pnNode.uAccountID);

		if (!!pnNode.uCurrencyID)
			jvNode["currency"]	= STAmount::createHumanCurrency(pnNode.uCurrencyID);

		if (!!pnNode.uIssuerID)
			jvNode["issuer"]	= RippleAddress::createHumanAccountID(pnNode.uIssuerID);

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

TER RippleCalc::calcNodeFwd(const unsigned int uNode, PathState::ref pspCur, const bool bMultiQuality)
{
	const PaymentNode&		pnCur		= pspCur->vpnNodes[uNode];
	const bool				bCurAccount	= isSetBit(pnCur.uFlags,  STPathElement::typeAccount);

	cLog(lsINFO) << boost::str(boost::format("calcNodeFwd> uNode=%d") % uNode);

	TER						terResult	= bCurAccount
											? calcNodeAccountFwd(uNode, pspCur, bMultiQuality)
											: calcNodeOfferFwd(uNode, pspCur, bMultiQuality);

	if (tesSUCCESS == terResult && uNode + 1 != pspCur->vpnNodes.size())
	{
		terResult	= calcNodeFwd(uNode+1, pspCur, bMultiQuality);
	}

	cLog(lsINFO) << boost::str(boost::format("calcNodeFwd< uNode=%d terResult=%d") % uNode % terResult);

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
TER RippleCalc::calcNodeRev(const unsigned int uNode, PathState::ref pspCur, const bool bMultiQuality)
{
	PaymentNode&	pnCur		= pspCur->vpnNodes[uNode];
	const bool		bCurAccount	= isSetBit(pnCur.uFlags,  STPathElement::typeAccount);
	TER				terResult;

	// Do current node reverse.
	const uint160&	uCurIssuerID	= pnCur.uIssuerID;
	STAmount&		saTransferRate	= pnCur.saTransferRate;

		saTransferRate	= STAmount::saFromRate(lesActive.rippleTransferRate(uCurIssuerID));

	cLog(lsINFO) << boost::str(boost::format("calcNodeRev> uNode=%d uIssuerID=%s saTransferRate=%s")
		% uNode
		% RippleAddress::createHumanAccountID(uCurIssuerID)
		% saTransferRate.getFullText());

	terResult	= bCurAccount
					? calcNodeAccountRev(uNode, pspCur, bMultiQuality)
					: calcNodeOfferRev(uNode, pspCur, bMultiQuality);

	// Do previous.
	if (tesSUCCESS != terResult)
	{
		// Error, don't continue.
		nothing();
	}
	else if (uNode)
	{
		// Continue in reverse.

		terResult	= calcNodeRev(uNode-1, pspCur, bMultiQuality);
	}

	cLog(lsINFO) << boost::str(boost::format("calcNodeRev< uNode=%d terResult=%s/%d") % uNode % transToken(terResult) % terResult);

	return terResult;
}

// Calculate the next increment of a path.
// The increment is what can satisfy a portion or all of the requested output at the best quality.
// <-- pspCur->uQuality
void RippleCalc::pathNext(PathState::ref pspCur, const int iPaths, const LedgerEntrySet& lesCheckpoint, LedgerEntrySet& lesCurrent)
{
	// The next state is what is available in preference order.
	// This is calculated when referenced accounts changed.
	const bool			bMultiQuality	= iPaths == 1;
	const unsigned int	uLast			= pspCur->vpnNodes.size() - 1;

	pspCur->bConsumed	= false;

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
	}

	if (tesSUCCESS == pspCur->terStatus)
	{
		tLog(!pspCur->saInPass || !pspCur->saOutPass, lsDEBUG)
			<< boost::str(boost::format("saOutPass=%s saInPass=%s")
				% pspCur->saOutPass.getFullText()
				% pspCur->saInPass.getFullText());

		assert(!!pspCur->saOutPass && !!pspCur->saInPass);

		pspCur->uQuality	= STAmount::getRate(pspCur->saOutPass, pspCur->saInPass);	// Calculate relative quality.

		cLog(lsINFO) << "Path after forward: " << pspCur->getJson();
	}
	else
	{
		pspCur->uQuality	= 0;
	}
}

TER RippleCalc::rippleCalc(
	// Compute paths vs this ledger entry set.  Up to caller to actually apply to ledger.
	LedgerEntrySet&		lesActive,				// <-> --> = Fee already applied to src balance.
		  STAmount&		saMaxAmountAct,			// <-- The computed input amount.
		  STAmount&		saDstAmountAct,			// <-- The computed output amount.

	// Issuer:
	//      XRP: ACCOUNT_XRP
	//  non-XRP: uSrcAccountID (for any issuer) or another account with trust node.
	const STAmount&		saMaxAmountReq,			// --> -1 = no limit.

	// Issuer:
	//      XRP: ACCOUNT_XRP
	//  non-XRP: uDstAccountID (for any issuer) or another account with trust node.
	const STAmount&		saDstAmountReq,

	const uint160&		uDstAccountID,
	const uint160&		uSrcAccountID,
	const STPathSet&	spsPaths,
    const bool			bPartialPayment,
    const bool			bLimitQuality,
    const bool			bNoRippleDirect,
	const bool			bStandAlone				// True, not to delete unfundeds.
    )
{
	RippleCalc		rc(lesActive);

    TER	    terResult	= temUNCERTAIN;

	// YYY Might do basic checks on src and dst validity as per doPayment.

    if (bNoRippleDirect && spsPaths.isEmpty())
    {
	    cLog(lsINFO) << "rippleCalc: Invalid transaction: No paths and direct ripple not allowed.";

	    return temRIPPLE_EMPTY;
    }

    // Incrementally search paths.
    std::vector<PathState::pointer>	vpsPaths;

    if (!bNoRippleDirect)
    {
	    // Direct path.
	    // XXX Might also make a XRP bridge by default.

	    PathState::pointer	pspDirect	= PathState::createPathState(
		    vpsPaths.size(),
		    lesActive,
		    STPath(),
		    uDstAccountID,
		    uSrcAccountID,
		    saDstAmountReq,
		    saMaxAmountReq);

cLog(lsDEBUG) << boost::str(boost::format("rippleCalc: Build direct: add: %d status: %s")
	% !!pspDirect
	% transToken(pspDirect ? pspDirect->terStatus : temUNKNOWN));
	    if (pspDirect)
	    {
		    // Return if malformed.
		    if (isTemMalformed(pspDirect->terStatus))
			    return pspDirect->terStatus;

		    if (tesSUCCESS == pspDirect->terStatus)
		    {
			    // Had a success.
			    terResult	= tesSUCCESS;

			    vpsPaths.push_back(pspDirect);
		    }
			else if (terNO_LINE != pspDirect->terStatus)
			{
			    terResult	= pspDirect->terStatus;
			}
	    }
    }

    cLog(lsINFO) << "rippleCalc: Paths in set: " << spsPaths.size();

int	iIndex	= 0;
    BOOST_FOREACH(const STPath& spPath, spsPaths)
    {
	    PathState::pointer	pspExpanded	= PathState::createPathState(
		    vpsPaths.size(),
		    lesActive,
		    spPath,
		    uDstAccountID,
		    uSrcAccountID,
		    saDstAmountReq,
		    saMaxAmountReq);

cLog(lsDEBUG) << boost::str(boost::format("rippleCalc: Build path: %d: add: %d status: %s")
	% ++iIndex
	% !!pspExpanded
	% transToken(pspExpanded ? pspExpanded->terStatus : temUNKNOWN));

	    if (pspExpanded)
	    {
		    // Return, if the path specification was malformed.
		    if (isTemMalformed(pspExpanded->terStatus))
			    return pspExpanded->terStatus;

		    if (tesSUCCESS == pspExpanded->terStatus) {
			    terResult	= tesSUCCESS;			// Had a success.

				vpsPaths.push_back(pspExpanded);
			}
			else if (terNO_LINE != pspExpanded->terStatus)
			{
			    terResult	= pspExpanded->terStatus;
			}
	    }
    }

    if (tesSUCCESS != terResult)
    {
	    return terResult == temUNCERTAIN ? terNO_LINE : terResult;
	}
    else
    {
	    terResult	= temUNCERTAIN;
    }

	saMaxAmountAct	= STAmount(saMaxAmountReq.getCurrency(), saMaxAmountReq.getIssuer());
	saDstAmountAct	= STAmount(saDstAmountReq.getCurrency(), saDstAmountReq.getIssuer());

    const LedgerEntrySet	lesBase			= lesActive;							// Checkpoint with just fees paid.
    const uint64			uQualityLimit	= bLimitQuality ? STAmount::getRate(saDstAmountReq, saMaxAmountReq) : 0;
	// When processing, don't want to complicate directory walking with deletion.
	std::vector<uint256>	vuUnfundedBecame;										// Offers that became unfunded.

int iPass	= 0;
    while (temUNCERTAIN == terResult)
    {
	    PathState::pointer		pspBest;
	    const LedgerEntrySet	lesCheckpoint	= lesActive;
		int						iDry			= 0;

	    // Find the best path.
	    BOOST_FOREACH(PathState::pointer& pspCur, vpsPaths)
	    {
		    if (pspCur->uQuality)
			{
				pspCur->saInAct		= saMaxAmountAct;										// Update to current amount processed.
				pspCur->saOutAct	= saDstAmountAct;

				rc.pathNext(pspCur, vpsPaths.size(), lesCheckpoint, lesActive);		// Compute increment.

				if (!pspCur->uQuality) {
					// Path was dry.

					++iDry;
				}
				else {
					tLog(!pspCur->saInPass || !pspCur->saOutPass, lsDEBUG)
						<< boost::str(boost::format("rippleCalc: better: uQuality=%s saInPass=%s saOutPass=%s")
							% STAmount::saFromRate(pspCur->uQuality)
							% pspCur->saInPass.getFullText()
							% pspCur->saOutPass.getFullText());

					assert(!!pspCur->saInPass && !!pspCur->saOutPass);

					if ((!bLimitQuality || pspCur->uQuality <= uQualityLimit)		// Quality is not limted or increment has allowed quality.
						&& (!pspBest												// Best is not yet set.
							|| PathState::lessPriority(pspBest, pspCur)))			// Current is better than set.
					{
						cLog(lsDEBUG) << boost::str(boost::format("rippleCalc: better: uQuality=%s saInPass=%s saOutPass=%s")
							% STAmount::saFromRate(pspCur->uQuality)
							% pspCur->saInPass.getFullText()
							% pspCur->saOutPass.getFullText());

						lesActive.swapWith(pspCur->lesEntries);							// For the path, save ledger state.
						pspBest	= pspCur;
					}
				}
			}
	    }
cLog(lsDEBUG) << boost::str(boost::format("rippleCalc: Summary: Pass: %d Dry: %d Paths: %d") % ++iPass % iDry % vpsPaths.size());
	    BOOST_FOREACH(PathState::pointer& pspCur, vpsPaths)
	    {
cLog(lsDEBUG) << boost::str(boost::format("rippleCalc: Summary: %d quality:%d best: %d consumed: %d") % pspCur->mIndex % pspCur->uQuality % (pspBest == pspCur) % pspCur->bConsumed);
		}

	    if (pspBest)
	    {
		    // Apply best path.

			cLog(lsDEBUG) << boost::str(boost::format("rippleCalc: best: uQuality=%s saInPass=%s saOutPass=%s")
				% STAmount::saFromRate(pspBest->uQuality)
				% pspBest->saInPass.getFullText()
				% pspBest->saOutPass.getFullText());

		    // Record best pass' offers that became unfunded for deletion on success.
		    vuUnfundedBecame.insert(vuUnfundedBecame.end(), pspBest->vUnfundedBecame.begin(), pspBest->vUnfundedBecame.end());

		    // Record best pass' LedgerEntrySet to build off of and potentially return.
		    lesActive.swapWith(pspBest->lesEntries);

			saMaxAmountAct		+= pspBest->saInPass;
			saDstAmountAct	+= pspBest->saOutPass;

			if (pspBest->bConsumed)
			{
				++iDry;
				pspBest->uQuality	= 0;
			}

		    if (saDstAmountAct == saDstAmountReq)
		    {
				// Done. Delivered requested amount.

			    terResult	= tesSUCCESS;
		    }
		    else if (saMaxAmountAct != saMaxAmountReq && iDry != vpsPaths.size())
		    {
			    // Have not met requested amount or max send, try to do more. Prepare for next pass.

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
	    else if (!saDstAmountAct)
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

	if (!bStandAlone)
	{
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
		// Transfer XRP.

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
				// No additional fees for XRP.

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
// If either currency is not XRP, then also calculates vs XRP bridge.
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

	// Direct: not bridging via XRP
	bool			bDirectNext	= true;		// True, if need to load.
	uint256			uDirectQuality;
	uint256			uDirectTip	= Ledger::getBookBase(uGetsCurrency, uGetsIssuerID, uPaysCurrency, uPaysIssuerID);
	uint256			uDirectEnd	= Ledger::getQualityNext(uDirectTip);

	// Bridging: bridging via XRP
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
		// Bridging: Neither currency is XRP.
		uInTip		= Ledger::getBookBase(uPrvCurrencyID, uPrvIssuerID, CURRENCY_XRP, ACCOUNT_XRP);
		uInEnd		= Ledger::getQualityNext(uInTip);
		uOutTip		= Ledger::getBookBase(CURRENCY_XRP, ACCOUNT_XRP, uCurCurrencyID, uCurIssuerID);
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
