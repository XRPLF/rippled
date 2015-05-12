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
#include <ripple/app/paths/cursor/RippleLiquidity.h>
#include <ripple/basics/Log.h>

namespace ripple {
namespace path {

// For current offer, get input from deliver/limbo and output to next account or
// deliver for next offers.
//
// <-- node.saFwdDeliver: For forwardLiquidityForAccount to know
//                        how much went through
// --> node.saRevDeliver: Do not exceed.

TER PathCursor::deliverNodeForward (
    Account const& uInAccountID,    // --> Input owner's account.
    STAmount const& saInReq,        // --> Amount to deliver.
    STAmount& saInAct,              // <-- Amount delivered, this invocation.
    STAmount& saInFees) const       // <-- Fees charged, this invocation.
{
    TER resultCode   = tesSUCCESS;

    // Don't deliver more than wanted.
    // Zeroed in reverse pass.
    node().directory.restart(multiQuality_);

    saInAct.clear (saInReq);
    saInFees.clear (saInReq);

    int loopCount = 0;

    // XXX Perhaps make sure do not exceed node().saRevDeliver as another way to
    // stop?
    while (resultCode == tesSUCCESS && saInAct + saInFees < saInReq)
    {
        // Did not spend all inbound deliver funds.
        if (++loopCount > CALC_NODE_DELIVER_MAX_LOOPS)
        {
            WriteLog (lsWARNING, RippleCalc)
                << "deliverNodeForward: max loops cndf";
            return telFAILED_PROCESSING;
        }

        // Determine values for pass to adjust saInAct, saInFees, and
        // node().saFwdDeliver.
        advanceNode (saInAct, false);

        // If needed, advance to next funded offer.

        if (resultCode != tesSUCCESS)
        {
        }
        else if (!node().offerIndex_)
        {
            WriteLog (lsWARNING, RippleCalc)
                << "deliverNodeForward: INTERNAL ERROR: Ran out of offers.";
            return telFAILED_PROCESSING;
        }
        else if (resultCode == tesSUCCESS)
        {
            // Doesn't charge input. Input funds are in limbo.
            // There's no fee if we're transferring XRP, if the sender is the
            // issuer, or if the receiver is the issuer.
            bool noFee = isXRP (previousNode().issue_)
                || uInAccountID == previousNode().issue_.account
                || node().offerOwnerAccount_ == previousNode().issue_.account;
            const STAmount saInFeeRate = noFee ? saOne
                : previousNode().transferRate_;  // Transfer rate of issuer.

            // First calculate assuming no output fees: saInPassAct,
            // saInPassFees, saOutPassAct.

            // Offer maximum out - limited by funds with out fees.
            auto saOutFunded = std::min (
                node().saOfferFunds, node().saTakerGets);

            // Offer maximum out - limit by most to deliver.
            auto saOutPassFunded = std::min (
                saOutFunded,
                node().saRevDeliver - node().saFwdDeliver);

            // Offer maximum in - Limited by by payout.
            auto saInFunded = mulRound (
                saOutPassFunded,
                node().saOfrRate,
                node().saTakerPays.issue (),
                true);

            // Offer maximum in with fees.
            auto saInTotal = mulRound (saInFunded, saInFeeRate,
                saInFunded.issue (), true);
            auto saInRemaining = saInReq - saInAct - saInFees;

            if (saInRemaining < zero)
                saInRemaining.clear();

            // In limited by remaining.
            auto saInSum = std::min (saInTotal, saInRemaining);

            // In without fees.
            auto saInPassAct = std::min (
                node().saTakerPays, divRound (
                    saInSum, saInFeeRate, saInSum.issue (), true));

            // Out limited by in remaining.
            auto outPass = divRound (
                saInPassAct, node().saOfrRate, node().saTakerGets.issue (), true);
            STAmount saOutPassMax    = std::min (saOutPassFunded, outPass);

            STAmount saInPassFeesMax = saInSum - saInPassAct;

            // Will be determined by next node().
            STAmount saOutPassAct;

            // Will be determined by adjusted saInPassAct.
            STAmount saInPassFees;

            WriteLog (lsTRACE, RippleCalc)
                << "deliverNodeForward:"
                << " nodeIndex_=" << nodeIndex_
                << " saOutFunded=" << saOutFunded
                << " saOutPassFunded=" << saOutPassFunded
                << " node().saOfferFunds=" << node().saOfferFunds
                << " node().saTakerGets=" << node().saTakerGets
                << " saInReq=" << saInReq
                << " saInAct=" << saInAct
                << " saInFees=" << saInFees
                << " saInFunded=" << saInFunded
                << " saInTotal=" << saInTotal
                << " saInSum=" << saInSum
                << " saInPassAct=" << saInPassAct
                << " saOutPassMax=" << saOutPassMax;

            // FIXME: We remove an offer if WE didn't want anything out of it?
            if (!node().saTakerPays || saInSum <= zero)
            {
                WriteLog (lsDEBUG, RippleCalc)
                    << "deliverNodeForward: Microscopic offer unfunded.";

                // After math offer is effectively unfunded.
                pathState_.unfundedOffers().push_back (node().offerIndex_);
                node().bEntryAdvance = true;
                continue;
            }

            if (!saInFunded)
            {
                // Previous check should catch this.
                WriteLog (lsWARNING, RippleCalc)
                    << "deliverNodeForward: UNREACHABLE REACHED";

                // After math offer is effectively unfunded.
                pathState_.unfundedOffers().push_back (node().offerIndex_);
                node().bEntryAdvance = true;
                continue;
            }

            if (!isXRP(nextNode().account_))
            {
                // ? --> OFFER --> account
                // Input fees: vary based upon the consumed offer's owner.
                // Output fees: none as XRP or the destination account is the
                // issuer.

                saOutPassAct = saOutPassMax;
                saInPassFees = saInPassFeesMax;

                WriteLog (lsTRACE, RippleCalc)
                    << "deliverNodeForward: ? --> OFFER --> account:"
                    << " offerOwnerAccount_="
                    << node().offerOwnerAccount_
                    << " nextNode().account_="
                    << nextNode().account_
                    << " saOutPassAct=" << saOutPassAct
                    << " saOutFunded=" << saOutFunded;

                // Output: Debit offer owner, send XRP or non-XPR to next
                // account.
                resultCode = ledger().accountSend (
                    node().offerOwnerAccount_,
                    nextNode().account_,
                    saOutPassAct);

                if (resultCode != tesSUCCESS)
                    break;
            }
            else
            {
                // ? --> OFFER --> offer
                //
                // Offer to offer means current order book's output currency and
                // issuer match next order book's input current and issuer.
                //
                // Output fees: possible if issuer has fees and is not on either
                // side.
                STAmount saOutPassFees;

                // Output fees vary as the next nodes offer owners may vary.
                // Therefore, immediately push through output for current offer.
                resultCode = increment().deliverNodeForward (
                    node().offerOwnerAccount_,  // --> Current holder.
                    saOutPassMax,             // --> Amount available.
                    saOutPassAct,             // <-- Amount delivered.
                    saOutPassFees);           // <-- Fees charged.

                if (resultCode != tesSUCCESS)
                    break;

                if (saOutPassAct == saOutPassMax)
                {
                    // No fees and entire output amount.

                    saInPassFees = saInPassFeesMax;
                }
                else
                {
                    // Fraction of output amount.
                    // Output fees are paid by offer owner and not passed to
                    // previous.

                    assert (saOutPassAct < saOutPassMax);
                    auto inPassAct = mulRound (
                        saOutPassAct, node().saOfrRate, saInReq.issue (), true);
                    saInPassAct = std::min (node().saTakerPays, inPassAct);
                    auto inPassFees = mulRound (
                        saInPassAct, saInFeeRate, saInPassAct.issue (), true);
                    saInPassFees    = std::min (saInPassFeesMax, inPassFees);
                }

                // Do outbound debiting.
                // Send to issuer/limbo total amount including fees (issuer gets
                // fees).
                auto const& id = isXRP(node().issue_) ?
                        xrpAccount() : node().issue_.account;
                auto outPassTotal = saOutPassAct + saOutPassFees;
                ledger().accountSend (
                    node().offerOwnerAccount_,
                    id,
                    outPassTotal);

                WriteLog (lsTRACE, RippleCalc)
                    << "deliverNodeForward: ? --> OFFER --> offer:"
                    << " saOutPassAct=" << saOutPassAct
                    << " saOutPassFees=" << saOutPassFees;
            }

            WriteLog (lsTRACE, RippleCalc)
                << "deliverNodeForward: "
                << " nodeIndex_=" << nodeIndex_
                << " node().saTakerGets=" << node().saTakerGets
                << " node().saTakerPays=" << node().saTakerPays
                << " saInPassAct=" << saInPassAct
                << " saInPassFees=" << saInPassFees
                << " saOutPassAct=" << saOutPassAct
                << " saOutFunded=" << saOutFunded;

            // Funds were spent.
            node().bFundsDirty = true;

            // Do inbound crediting.
            //
            // Credit offer owner from in issuer/limbo (input transfer fees left
            // with owner).  Don't attempt to have someone credit themselves, it
            // is redundant.
            if (isXRP (previousNode().issue_.currency)
                || uInAccountID != node().offerOwnerAccount_)
            {
                auto id = !isXRP(previousNode().issue_.currency) ?
                        uInAccountID : xrpAccount();
                resultCode = ledger().accountSend (
                    id,
                    node().offerOwnerAccount_,
                    saInPassAct);

                if (resultCode != tesSUCCESS)
                    break;
            }

            // Adjust offer.
            //
            // Fees are considered paid from a seperate budget and are not named
            // in the offer.
            STAmount saTakerGetsNew  = node().saTakerGets - saOutPassAct;
            STAmount saTakerPaysNew  = node().saTakerPays - saInPassAct;

            if (saTakerPaysNew < zero || saTakerGetsNew < zero)
            {
                WriteLog (lsWARNING, RippleCalc)
                    << "deliverNodeForward: NEGATIVE:"
                    << " saTakerPaysNew=" << saTakerPaysNew
                    << " saTakerGetsNew=" << saTakerGetsNew;

                resultCode = telFAILED_PROCESSING;
                break;
            }

            node().sleOffer->setFieldAmount (sfTakerGets, saTakerGetsNew);
            node().sleOffer->setFieldAmount (sfTakerPays, saTakerPaysNew);

            ledger().entryModify (node().sleOffer);

            if (saOutPassAct == saOutFunded || saTakerGetsNew == zero)
            {
                // Offer became unfunded.

                WriteLog (lsDEBUG, RippleCalc)
                    << "deliverNodeForward: unfunded:"
                    << " saOutPassAct=" << saOutPassAct
                    << " saOutFunded=" << saOutFunded;

                pathState_.unfundedOffers().push_back (node().offerIndex_);
                node().bEntryAdvance   = true;
            }
            else
            {
                CondLog (saOutPassAct >= saOutFunded, lsWARNING, RippleCalc)
                    << "deliverNodeForward: TOO MUCH:"
                    << " saOutPassAct=" << saOutPassAct
                    << " saOutFunded=" << saOutFunded;

                assert (saOutPassAct < saOutFunded);
            }

            saInAct += saInPassAct;
            saInFees += saInPassFees;

            // Adjust amount available to next node().
            node().saFwdDeliver = std::min (node().saRevDeliver,
                                        node().saFwdDeliver + saOutPassAct);
        }
    }

    WriteLog (lsTRACE, RippleCalc)
        << "deliverNodeForward<"
        << " nodeIndex_=" << nodeIndex_
        << " saInAct=" << saInAct
        << " saInFees=" << saInFees;

    return resultCode;
}

} // path
} // ripple
