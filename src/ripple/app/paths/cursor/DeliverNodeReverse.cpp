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

#include <BeastConfig.h>
#include <ripple/app/paths/cursor/EffectiveRate.h>
#include <ripple/app/paths/cursor/RippleLiquidity.h>
#include <ripple/basics/Log.h>

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
TER PathCursor::deliverNodeReverseImpl (
    AccountID const& uOutAccountID, // --> Output owner's account.
    STAmount const& saOutReq,       // --> Funds requested to be
                                    // delivered for an increment.
    STAmount& saOutAct,             // <-- Funds actually delivered for an
                                    // increment
    bool callerHasLiquidity
                                        ) const
{
    TER resultCode   = tesSUCCESS;

    // Accumulation of what the previous node must deliver.
    // Possible optimization: Note this gets zeroed on each increment, ideally
    // only on first increment, then it could be a limit on the forward pass.
    saOutAct.clear (saOutReq);

    JLOG (j_.trace())
        << "deliverNodeReverse>"
        << " saOutAct=" << saOutAct
        << " saOutReq=" << saOutReq
        << " saPrvDlvReq=" << previousNode().saRevDeliver;

    assert (saOutReq != zero);

    int loopCount = 0;
    auto viewJ = rippleCalc_.logs_.journal ("View");

    // While we did not deliver as much as requested:
    while (saOutAct < saOutReq)
    {
        if (++loopCount >
            (multiQuality_ ?
                CALC_NODE_DELIVER_MAX_LOOPS_MQ :
                CALC_NODE_DELIVER_MAX_LOOPS))
        {
            JLOG (j_.warn()) << "loop count exceeded";
            return telFAILED_PROCESSING;
        }

        resultCode = advanceNode (saOutAct, true, callerHasLiquidity);
        // If needed, advance to next funded offer.

        if (resultCode != tesSUCCESS || !node().offerIndex_)
            // Error or out of offers.
            break;

        auto const xferRate = effectiveRate (
            node().issue_,
            uOutAccountID,
            node().offerOwnerAccount_,
            node().transferRate_);

        JLOG (j_.trace())
            << "deliverNodeReverse:"
            << " offerOwnerAccount_=" << node().offerOwnerAccount_
            << " uOutAccountID=" << uOutAccountID
            << " node().issue_.account=" << node().issue_.account
            << " xferRate=" << xferRate;

        // Only use rate when not in multi-quality mode
        if (!multiQuality_)
        {
            if (!node().rateMax)
            {
                // Set initial rate.
                JLOG (j_.trace())
                    << "Set initial rate";

                node().rateMax = xferRate;
            }
            else if (xferRate > node().rateMax)
            {
                // Offer exceeds initial rate.
                JLOG (j_.trace())
                    << "Offer exceeds initial rate: " << *node().rateMax;

                break;  // Done. Don't bother looking for smaller transferRates.
            }
            else if (xferRate < node().rateMax)
            {
                // Reducing rate. Additional offers will only
                // be considered for this increment if they
                // are at least this good.
                //
                // At this point, the overall rate is reducing,
                // while the overall rate is not xferRate, it
                // would be wrong to add anything with a rate
                // above xferRate.
                //
                // The rate would be reduced if the current
                // offer was from the issuer and the previous
                // offer wasn't.

                JLOG (j_.trace())
                    << "Reducing rate: " << *node().rateMax;

                node().rateMax = xferRate;
            }
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
        STAmount saOutPlusFees   = multiplyRound (
            saOutPassAct, xferRate, false);


        // Offer out with fees.

        JLOG (j_.trace())
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
            auto fee = divideRound (saOutPlusFees, xferRate, true);
            saOutPassAct = std::min (saOutPassReq, fee);

            JLOG (j_.trace())
                << "deliverNodeReverse: Total exceeds fees:"
                << " saOutPassAct=" << saOutPassAct
                << " saOutPlusFees=" << saOutPlusFees
                << " node().saOfferFunds=" << node().saOfferFunds;
        }

        // Compute portion of input needed to cover actual output.
        auto outputFee = mulRound (
            saOutPassAct, node().saOfrRate, node().saTakerPays.issue (), true);
        if (*stAmountCalcSwitchover == false && ! outputFee)
        {
            JLOG (j_.fatal())
                << "underflow computing outputFee "
                << "saOutPassAct: " << saOutPassAct
                << " saOfrRate: " << node ().saOfrRate;
            return telFAILED_PROCESSING;
        }
        STAmount saInPassReq = std::min (node().saTakerPays, outputFee);
        STAmount saInPassAct;

        JLOG (j_.trace())
            << "deliverNodeReverse:"
            << " outputFee=" << outputFee
            << " saInPassReq=" << saInPassReq
            << " node().saOfrRate=" << node().saOfrRate
            << " saOutPassAct=" << saOutPassAct
            << " saOutPlusFees=" << saOutPlusFees;

        if (!saInPassReq) // FIXME: This is bogus
        {
            // After rounding did not want anything.
            JLOG (j_.debug())
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

            JLOG (j_.trace())
                << "deliverNodeReverse: account --> OFFER --> ? :"
                << " saInPassAct=" << saInPassAct;
        }
        else
        {
            // offer --> OFFER --> ?
            // Compute in previous offer node how much could come in.

            // TODO(tom): Fix nasty recursion here!
            resultCode = increment(-1).deliverNodeReverseImpl(
                node().offerOwnerAccount_,
                saInPassReq,
                saInPassAct,
                saOutAct > zero);

            if (fix1141(view().info().parentCloseTime))
            {
                // The recursive call is dry this time, but we have liquidity
                // from previous calls
                if (resultCode == tecPATH_DRY && saOutAct > zero)
                {
                    resultCode = tesSUCCESS;
                    break;
                }
            }

            JLOG (j_.trace())
                << "deliverNodeReverse: offer --> OFFER --> ? :"
                << " saInPassAct=" << saInPassAct;
        }

        if (resultCode != tesSUCCESS)
            break;

        if (saInPassAct < saInPassReq)
        {
            // Adjust output to conform to limited input.
            auto outputRequirements = divRound (saInPassAct, node ().saOfrRate,
                node ().saTakerGets.issue (), true);
            saOutPassAct = std::min (saOutPassReq, outputRequirements);
            auto outputFees = multiplyRound (saOutPassAct, xferRate, true);
            saOutPlusFees   = std::min (node().saOfferFunds, outputFees);

            JLOG (j_.trace())
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
        resultCode   = accountSend(view(),
            node().offerOwnerAccount_, node().issue_.account, saOutPassAct, viewJ);

        if (resultCode != tesSUCCESS)
            break;

        // Adjust offer
        STAmount saTakerGetsNew  = node().saTakerGets - saOutPassAct;
        STAmount saTakerPaysNew  = node().saTakerPays - saInPassAct;

        if (saTakerPaysNew < zero || saTakerGetsNew < zero)
        {
            JLOG (j_.warn())
                << "deliverNodeReverse: NEGATIVE:"
                << " node().saTakerPaysNew=" << saTakerPaysNew
                << " node().saTakerGetsNew=" << saTakerGetsNew;

            resultCode = telFAILED_PROCESSING;
            break;
        }

        node().sleOffer->setFieldAmount (sfTakerGets, saTakerGetsNew);
        node().sleOffer->setFieldAmount (sfTakerPays, saTakerPaysNew);

        view().update (node().sleOffer);

        if (saOutPassAct == node().saTakerGets)
        {
            // Offer became unfunded.
            JLOG (j_.debug())
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

    if (saOutAct > saOutReq)
    {
        JLOG (j_.warn())
            << "deliverNodeReverse: TOO MUCH:"
            << " saOutAct=" << saOutAct
            << " saOutReq=" << saOutReq;
    }

    assert(saOutAct <= saOutReq);

    if (resultCode == tesSUCCESS && !saOutAct)
        resultCode = tecPATH_DRY;
    // Unable to meet request, consider path dry.
    // Design invariant: if nothing was actually delivered, return tecPATH_DRY.

    JLOG (j_.trace())
        << "deliverNodeReverse<"
        << " saOutAct=" << saOutAct
        << " saOutReq=" << saOutReq
        << " saPrvDlvReq=" << previousNode().saRevDeliver;

    return resultCode;
}

TER PathCursor::deliverNodeReverse (
    AccountID const& uOutAccountID, // --> Output owner's account.
    STAmount const& saOutReq,       // --> Funds requested to be
                                    // delivered for an increment.
    STAmount& saOutAct              // <-- Funds actually delivered for an
                                    // increment
                                    ) const
{
    for (int i = nodeIndex_; i >= 0 && !node (i).isAccount(); --i)
        node (i).directory.restart (multiQuality_);

    return deliverNodeReverseImpl(uOutAccountID, saOutReq, saOutAct, /*callerHasLiquidity*/false);
}

}  // path
}  // ripple
