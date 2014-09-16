//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/app/paths/cursor/RippleLiquidity.h>

namespace ripple {
namespace path {

// At the right most node of a list of consecutive offer nodes, given the amount
// requested to be delivered, push towards the left nodes the amount requested
// for the right nodes so we can compute how much to deliver from the source.
//
// Between offer nodes, the fee charged may vary.  Therefore, process one
// inbound offer at a time.  Propagate the inbound offer's requirements to the
// previous node.  The previous node adjusts the amount output and the amount
// spent on fees.  Continue processing until the request is satisified as long
// as the rate does not increase past the initial rate.

// To deliver from an order book, when computing
TER PathCursor::deliverNodeReverse (
    Account const& uOutAccountID,  // --> Output owner's account.
    STAmount const& saOutReq,      // --> Funds requested to be
                                   // delivered for an increment.
    STAmount& saOutAct) const      // <-- Funds actually delivered for an
                                   // increment.
{
    TER resultCode   = tesSUCCESS;

    node().directory.restart(multiQuality_);

    // Accumulation of what the previous node must deliver.
    // Possible optimization: Note this gets zeroed on each increment, ideally
    // only on first increment, then it could be a limit on the forward pass.
    saOutAct.clear (saOutReq);

    WriteLog (lsTRACE, RippleCalc)
        << "deliverNodeReverse>"
        << " saOutAct=" << saOutAct
        << " saOutReq=" << saOutReq
        << " saPrvDlvReq=" << previousNode().saRevDeliver;

    assert (saOutReq != zero);

    int loopCount = 0;

    // While we did not deliver as much as requested:
    while (saOutAct < saOutReq)
    {
        if (++loopCount > CALC_NODE_DELIVER_MAX_LOOPS)
        {
            WriteLog (lsFATAL, RippleCalc) << "loop count exceeded";
            return telFAILED_PROCESSING;
        }

        resultCode = advanceNode (saOutAct, true);
        // If needed, advance to next funded offer.

        if (resultCode != tesSUCCESS || !node().offerIndex_)
            // Error or out of offers.
            break;

        auto const hasFee = node().offerOwnerAccount_ == node().issue_.account
            || uOutAccountID == node().issue_.account;
        // Issuer sending or receiving.

        const STAmount saOutFeeRate = hasFee
            ? saOne             // No fee.
            : node().transferRate_;   // Transfer rate of issuer.

        WriteLog (lsTRACE, RippleCalc)
            << "deliverNodeReverse:"
            << " offerOwnerAccount_="
            << node().offerOwnerAccount_
            << " uOutAccountID="
            << uOutAccountID
            << " node().issue_.account="
            << node().issue_.account
            << " node().transferRate_=" << node().transferRate_
            << " saOutFeeRate=" << saOutFeeRate;

        if (multiQuality_)
        {
            // In multi-quality mode, ignore rate.
        }
        else if (!node().saRateMax)
        {
            // Set initial rate.
            node().saRateMax = saOutFeeRate;

            WriteLog (lsTRACE, RippleCalc)
                << "deliverNodeReverse: Set initial rate:"
                << " node().saRateMax=" << node().saRateMax
                << " saOutFeeRate=" << saOutFeeRate;
        }
        else if (saOutFeeRate > node().saRateMax)
        {
            // Offer exceeds initial rate.
            WriteLog (lsTRACE, RippleCalc)
                << "deliverNodeReverse: Offer exceeds initial rate:"
                << " node().saRateMax=" << node().saRateMax
                << " saOutFeeRate=" << saOutFeeRate;

            break;  // Done. Don't bother looking for smaller transferRates.
        }
        else if (saOutFeeRate < node().saRateMax)
        {
            // Reducing rate. Additional offers will only considered for this
            // increment if they are at least this good.
            //
            // At this point, the overall rate is reducing, while the overall
            // rate is not saOutFeeRate, it would be wrong to add anything with
            // a rate above saOutFeeRate.
            //
            // The rate would be reduced if the current offer was from the
            // issuer and the previous offer wasn't.

            node().saRateMax   = saOutFeeRate;

            WriteLog (lsTRACE, RippleCalc)
                << "deliverNodeReverse: Reducing rate:"
                << " node().saRateMax=" << node().saRateMax;
        }

        // Amount that goes to the taker.
        STAmount saOutPassReq = std::min (
            std::min (node().saOfferFunds, node().saTakerGets),
            saOutReq - saOutAct);

        // Maximum out - assuming no out fees.
        STAmount saOutPassAct = saOutPassReq;

        // Amount charged to the offer owner.
        //
        // The fee goes to issuer. The fee is paid by offer owner and not passed
        // as a cost to taker.
        //
        // Round down: prefer liquidity rather than microscopic fees.
        STAmount saOutPlusFees   = mulRound (
            saOutPassAct, saOutFeeRate, false);
        // Offer out with fees.

        WriteLog (lsTRACE, RippleCalc)
            << "deliverNodeReverse:"
            << " saOutReq=" << saOutReq
            << " saOutAct=" << saOutAct
            << " node().saTakerGets=" << node().saTakerGets
            << " saOutPassAct=" << saOutPassAct
            << " saOutPlusFees=" << saOutPlusFees
            << " node().saOfferFunds=" << node().saOfferFunds;

        if (saOutPlusFees > node().saOfferFunds)
        {
            // Offer owner can not cover all fees, compute saOutPassAct based on
            // node().saOfferFunds.
            saOutPlusFees   = node().saOfferFunds;

            // Round up: prefer liquidity rather than microscopic fees. But,
            // limit by requested.
            auto fee = divRound (saOutPlusFees, saOutFeeRate, true);
            saOutPassAct = std::min (saOutPassReq, fee);

            WriteLog (lsTRACE, RippleCalc)
                << "deliverNodeReverse: Total exceeds fees:"
                << " saOutPassAct=" << saOutPassAct
                << " saOutPlusFees=" << saOutPlusFees
                << " node().saOfferFunds=" << node().saOfferFunds;
        }

        // Compute portion of input needed to cover actual output.
        auto outputFee = mulRound (
            saOutPassAct, node().saOfrRate, node().saTakerPays, true);
        STAmount saInPassReq = std::min (node().saTakerPays, outputFee);
        STAmount saInPassAct;

        WriteLog (lsTRACE, RippleCalc)
            << "deliverNodeReverse:"
            << " outputFee=" << outputFee
            << " saInPassReq=" << saInPassReq
            << " node().saOfrRate=" << node().saOfrRate
            << " saOutPassAct=" << saOutPassAct
            << " saOutPlusFees=" << saOutPlusFees;

        if (!saInPassReq) // FIXME: This is bogus
        {
            // After rounding did not want anything.
            WriteLog (lsDEBUG, RippleCalc)
                << "deliverNodeReverse: micro offer is unfunded.";

            node().bEntryAdvance   = true;
            continue;
        }
        // Find out input amount actually available at current rate.
        else if (!isXRP(previousNode().account_))
        {
            // account --> OFFER --> ?
            // Due to node expansion, previous is guaranteed to be the issuer.
            //
            // Previous is the issuer and receiver is an offer, so no fee or
            // quality.
            //
            // Previous is the issuer and has unlimited funds.
            //
            // Offer owner is obtaining IOUs via an offer, so credit line limits
            // are ignored.  As limits are ignored, don't need to adjust
            // previous account's balance.

            saInPassAct = saInPassReq;

            WriteLog (lsTRACE, RippleCalc)
                << "deliverNodeReverse: account --> OFFER --> ? :"
                << " saInPassAct=" << saInPassAct;
        }
        else
        {
            // offer --> OFFER --> ?
            // Compute in previous offer node how much could come in.

            // TODO(tom): Fix nasty recursion here!
            resultCode = increment(-1).deliverNodeReverse(
                node().offerOwnerAccount_,
                saInPassReq,
                saInPassAct);

            WriteLog (lsTRACE, RippleCalc)
                << "deliverNodeReverse: offer --> OFFER --> ? :"
                << " saInPassAct=" << saInPassAct;
        }

        if (resultCode != tesSUCCESS)
            break;

        if (saInPassAct < saInPassReq)
        {
            // Adjust output to conform to limited input.
            auto outputRequirements = divRound (
                saInPassAct, node().saOfrRate, node().saTakerGets, true);
            saOutPassAct = std::min (saOutPassReq, outputRequirements);
            auto outputFees = mulRound (
                saOutPassAct, saOutFeeRate, true);
            saOutPlusFees   = std::min (node().saOfferFunds, outputFees);

            WriteLog (lsTRACE, RippleCalc)
                << "deliverNodeReverse: adjusted:"
                << " saOutPassAct=" << saOutPassAct
                << " saOutPlusFees=" << saOutPlusFees;
        }
        else
        {
            // TODO(tom): more logging here.
            assert (saInPassAct == saInPassReq);
        }

        // Funds were spent.
        node().bFundsDirty = true;

        // Want to deduct output to limit calculations while computing reverse.
        // Don't actually need to send.
        //
        // Sending could be complicated: could fund a previous offer not yet
        // visited.  However, these deductions and adjustments are tenative.
        //
        // Must reset balances when going forward to perform actual transfers.
        resultCode   = ledger().accountSend (
            node().offerOwnerAccount_, node().issue_.account, saOutPassAct);

        if (resultCode != tesSUCCESS)
            break;

        // Adjust offer
        STAmount saTakerGetsNew  = node().saTakerGets - saOutPassAct;
        STAmount saTakerPaysNew  = node().saTakerPays - saInPassAct;

        if (saTakerPaysNew < zero || saTakerGetsNew < zero)
        {
            WriteLog (lsWARNING, RippleCalc)
                << "deliverNodeReverse: NEGATIVE:"
                << " node().saTakerPaysNew=" << saTakerPaysNew
                << " node().saTakerGetsNew=%s" << saTakerGetsNew;

            resultCode = telFAILED_PROCESSING;
            break;
        }

        node().sleOffer->setFieldAmount (sfTakerGets, saTakerGetsNew);
        node().sleOffer->setFieldAmount (sfTakerPays, saTakerPaysNew);

        ledger().entryModify (node().sleOffer);

        if (saOutPassAct == node().saTakerGets)
        {
            // Offer became unfunded.
            WriteLog (lsDEBUG, RippleCalc)
                << "deliverNodeReverse: offer became unfunded.";

            node().bEntryAdvance   = true;
            // XXX When don't we want to set advance?
        }
        else
        {
            assert (saOutPassAct < node().saTakerGets);
        }

        saOutAct += saOutPassAct;
        // Accumulate what is to be delivered from previous node.
        previousNode().saRevDeliver += saInPassAct;
    }

    CondLog (saOutAct > saOutReq, lsWARNING, RippleCalc)
        << "deliverNodeReverse: TOO MUCH:"
        << " saOutAct=" << saOutAct
        << " saOutReq=" << saOutReq;

    assert (saOutAct <= saOutReq);

    if (resultCode == tesSUCCESS && !saOutAct)
        resultCode = tecPATH_DRY;
    // Unable to meet request, consider path dry.
    // Design invariant: if nothing was actually delivered, return tecPATH_DRY.

    WriteLog (lsTRACE, RippleCalc)
        << "deliverNodeReverse<"
        << " saOutAct=" << saOutAct
        << " saOutReq=" << saOutReq
        << " saPrvDlvReq=" << previousNode().saRevDeliver;

    return resultCode;
}

}  // path
}  // ripple
