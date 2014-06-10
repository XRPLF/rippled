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

#include <ripple/module/app/paths/Calculators.h>
#include <ripple/module/app/paths/RippleCalc.h>
#include <ripple/module/app/paths/Tuning.h>

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

TER nodeDeliverRev (
    RippleCalc& rippleCalc,
    const unsigned int nodeIndex,
    PathState&         pathState,
    const bool         bMultiQuality,  // True, if not constrained to the same
                                       // or better quality.
    const uint160&     uOutAccountID,  // --> Output owner's account.
    const STAmount&    saOutReq,       // --> Funds requested to be
                                       // delivered for an increment.
    STAmount&          saOutAct)       // <-- Funds actually delivered for an
                                       // increment.
{
    TER resultCode   = tesSUCCESS;

    auto&    previousNode       = pathState.nodes()[nodeIndex - 1];
    auto&    node       = pathState.nodes()[nodeIndex];

    STAmount&       saPrvDlvReq     = previousNode.saRevDeliver;
    // Accumulation of what the previous node must deliver.

    uint256&        uDirectTip      = node.uDirectTip;
    bool&           bDirectRestart  = node.bDirectRestart;

    if (bMultiQuality)
        uDirectTip      = 0;                        // Restart book searching.
    else
        bDirectRestart  = true;                     // Restart at same quality.

    // Possible optimization: Note this gets zeroed on each increment, ideally
    // only on first increment, then it could be a limit on the forward pass.
    saOutAct.clear (saOutReq);

    WriteLog (lsTRACE, RippleCalc)
        << "nodeDeliverRev>"
        << " saOutAct=" << saOutAct
        << " saOutReq=" << saOutReq
        << " saPrvDlvReq=" << saPrvDlvReq;

    assert (saOutReq != zero);

    int loopCount = 0;

    // While we did not deliver as much as requested:
    while (saOutAct < saOutReq)
    {
        if (++loopCount > CALC_NODE_DELIVER_MAX_LOOPS)
        {
            WriteLog (lsFATAL, RippleCalc) << "loop count exceeded";
            return rippleCalc.mOpenLedger ? telFAILED_PROCESSING :
                    tecFAILED_PROCESSING;
        }

        bool&           bEntryAdvance   = node.bEntryAdvance;
        STAmount&       saOfrRate       = node.saOfrRate;
        SLE::pointer&   sleOffer        = node.sleOffer;
        bool&           bFundsDirty     = node.bFundsDirty;
        STAmount&       saOfferFunds    = node.saOfferFunds;
        STAmount&       saTakerPays     = node.saTakerPays;
        STAmount&       saTakerGets     = node.saTakerGets;
        STAmount&       saRateMax       = node.saRateMax;

        resultCode = nodeAdvance (
            rippleCalc,
            nodeIndex, pathState, bMultiQuality || saOutAct == zero, true);
        // If needed, advance to next funded offer.

        if (resultCode != tesSUCCESS || !node.offerIndex_)
        {
            // Error or out of offers.
            break;
        }

        auto const hasFee = node.offerOwnerAccount_ == node.issuer_
            || uOutAccountID == node.issuer_;  // Issuer sending or receiving.
        const STAmount saOutFeeRate = hasFee
            ? saOne             // No fee.
            : node.transferRate_;   // Transfer rate of issuer.

        WriteLog (lsTRACE, RippleCalc)
            << "nodeDeliverRev:"
            << " offerOwnerAccount_="
            << RippleAddress::createHumanAccountID (node.offerOwnerAccount_)
            << " uOutAccountID="
            << RippleAddress::createHumanAccountID (uOutAccountID)
            << " node.issuer_="
            << RippleAddress::createHumanAccountID (node.issuer_)
            << " node.transferRate_=" << node.transferRate_
            << " saOutFeeRate=" << saOutFeeRate;

        if (bMultiQuality)
        {
            // In multi-quality mode, ignore rate.
        }
        else if (!saRateMax)
        {
            // Set initial rate.
            saRateMax   = saOutFeeRate;

            WriteLog (lsTRACE, RippleCalc)
                << "nodeDeliverRev: Set initial rate:"
                << " saRateMax=" << saRateMax
                << " saOutFeeRate=" << saOutFeeRate;
        }
        else if (saOutFeeRate > saRateMax)
        {
            // Offer exceeds initial rate.
            WriteLog (lsTRACE, RippleCalc)
                << "nodeDeliverRev: Offer exceeds initial rate:"
                << " saRateMax=" << saRateMax
                << " saOutFeeRate=" << saOutFeeRate;

            break;  // Done. Don't bother looking for smaller transferRates.
        }
        else if (saOutFeeRate < saRateMax)
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

            saRateMax   = saOutFeeRate;

            WriteLog (lsTRACE, RippleCalc)
                << "nodeDeliverRev: Reducing rate:"
                << " saRateMax=" << saRateMax;
        }

        // Amount that goes to the taker.
        STAmount saOutPassReq = std::min (
            std::min (saOfferFunds, saTakerGets),
            saOutReq - saOutAct);

        // Maximum out - assuming no out fees.
        STAmount saOutPassAct = saOutPassReq;

        // Amount charged to the offer owner.
        //
        // The fee goes to issuer. The fee is paid by offer owner and not passed
        // as a cost to taker.
        //
        // Round down: prefer liquidity rather than microscopic fees.
        STAmount saOutPlusFees   = STAmount::mulRound (
            saOutPassAct, saOutFeeRate, false);
        // Offer out with fees.

        WriteLog (lsTRACE, RippleCalc)
            << "nodeDeliverRev:"
            << " saOutReq=" << saOutReq
            << " saOutAct=" << saOutAct
            << " saTakerGets=" << saTakerGets
            << " saOutPassAct=" << saOutPassAct
            << " saOutPlusFees=" << saOutPlusFees
            << " saOfferFunds=" << saOfferFunds;

        if (saOutPlusFees > saOfferFunds)
        {
            // Offer owner can not cover all fees, compute saOutPassAct based on
            // saOfferFunds.
            saOutPlusFees   = saOfferFunds;

            // Round up: prefer liquidity rather than microscopic fees. But,
            // limit by requested.
            auto fee = STAmount::divRound (saOutPlusFees, saOutFeeRate, true);
            saOutPassAct = std::min (saOutPassReq, fee);

            WriteLog (lsTRACE, RippleCalc)
                << "nodeDeliverRev: Total exceeds fees:"
                << " saOutPassAct=" << saOutPassAct
                << " saOutPlusFees=" << saOutPlusFees
                << " saOfferFunds=" << saOfferFunds;
        }

        // Compute portion of input needed to cover actual output.
        auto outputFee = STAmount::mulRound (
            saOutPassAct, saOfrRate, saTakerPays, true);
        STAmount saInPassReq = std::min (saTakerPays, outputFee);
        STAmount saInPassAct;

        WriteLog (lsTRACE, RippleCalc)
            << "nodeDeliverRev:"
            << " outputFee=" << outputFee
            << " saInPassReq=" << saInPassReq
            << " saOfrRate=" << saOfrRate
            << " saOutPassAct=" << saOutPassAct
            << " saOutPlusFees=" << saOutPlusFees;

        if (!saInPassReq) // FIXME: This is bogus
        {
            // After rounding did not want anything.
            WriteLog (lsDEBUG, RippleCalc)
                << "nodeDeliverRev: micro offer is unfunded.";

            bEntryAdvance   = true;
            continue;
        }
        // Find out input amount actually available at current rate.
        else if (!!previousNode.account_)
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
                << "nodeDeliverRev: account --> OFFER --> ? :"
                << " saInPassAct=" << saInPassAct;
        }
        else
        {
            // offer --> OFFER --> ?
            // Compute in previous offer node how much could come in.

            resultCode   = nodeDeliverRev (
                rippleCalc,
                nodeIndex - 1,
                pathState,
                bMultiQuality,
                node.offerOwnerAccount_,
                saInPassReq,
                saInPassAct);

            WriteLog (lsTRACE, RippleCalc)
                << "nodeDeliverRev: offer --> OFFER --> ? :"
                << " saInPassAct=" << saInPassAct;
        }

        if (resultCode != tesSUCCESS)
            break;

        if (saInPassAct < saInPassReq)
        {
            // Adjust output to conform to limited input.
            auto outputRequirements = STAmount::divRound (
                saInPassAct, saOfrRate, saTakerGets, true);
            saOutPassAct = std::min (saOutPassReq, outputRequirements);
            auto outputFees = STAmount::mulRound (
                saOutPassAct, saOutFeeRate, true);
            saOutPlusFees   = std::min (saOfferFunds, outputFees);

            WriteLog (lsTRACE, RippleCalc)
                << "nodeDeliverRev: adjusted:"
                << " saOutPassAct=" << saOutPassAct
                << " saOutPlusFees=" << saOutPlusFees;
        }
        else
        {
            // TODO(tom): more logging here.
            assert (saInPassAct == saInPassReq);
        }

        // Funds were spent.
        bFundsDirty = true;

        // Want to deduct output to limit calculations while computing reverse.
        // Don't actually need to send.
        //
        // Sending could be complicated: could fund a previous offer not yet
        // visited.  However, these deductions and adjustments are tenative.
        //
        // Must reset balances when going forward to perform actual transfers.
        resultCode   = rippleCalc.mActiveLedger.accountSend (
            node.offerOwnerAccount_, node.issuer_, saOutPassAct);

        if (resultCode != tesSUCCESS)
            break;

        // Adjust offer
        STAmount saTakerGetsNew  = saTakerGets - saOutPassAct;
        STAmount saTakerPaysNew  = saTakerPays - saInPassAct;

        if (saTakerPaysNew < zero || saTakerGetsNew < zero)
        {
            WriteLog (lsWARNING, RippleCalc)
                << "nodeDeliverRev: NEGATIVE:"
                << " saTakerPaysNew=" << saTakerPaysNew
                << " saTakerGetsNew=%s" << saTakerGetsNew;

            // If mOpenLedger then ledger is not final, can vote no.
            resultCode = rippleCalc.mOpenLedger ? telFAILED_PROCESSING
                    : tecFAILED_PROCESSING;
            break;
        }

        sleOffer->setFieldAmount (sfTakerGets, saTakerGetsNew);
        sleOffer->setFieldAmount (sfTakerPays, saTakerPaysNew);

        rippleCalc.mActiveLedger.entryModify (sleOffer);

        if (saOutPassAct == saTakerGets)
        {
            // Offer became unfunded.
            WriteLog (lsDEBUG, RippleCalc)
                << "nodeDeliverRev: offer became unfunded.";

            bEntryAdvance   = true;    // XXX When don't we want to set advance?
        }
        else
        {
            assert (saOutPassAct < saTakerGets);
        }

        saOutAct += saOutPassAct;
        // Accumulate what is to be delivered from previous node.
        saPrvDlvReq += saInPassAct;
    }

    CondLog (saOutAct > saOutReq, lsWARNING, RippleCalc)
        << "nodeDeliverRev: TOO MUCH:"
        << " saOutAct=" << saOutAct
        << " saOutReq=" << saOutReq;

    assert (saOutAct <= saOutReq);

    if (resultCode == tesSUCCESS && !saOutAct)
        resultCode = tecPATH_DRY;
    // Unable to meet request, consider path dry.
    // Design invariant: if nothing was actually delivered, return tecPATH_DRY.

    WriteLog (lsTRACE, RippleCalc)
        << "nodeDeliverRev<"
        << " saOutAct=" << saOutAct
        << " saOutReq=" << saOutReq
        << " saPrvDlvReq=" << saPrvDlvReq;

    return resultCode;
}

}  // path
}  // ripple
