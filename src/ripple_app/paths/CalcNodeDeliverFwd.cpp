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

#include "Calculators.h"
#include "RippleCalc.h"
#include "Tuning.h"

namespace ripple {

// For current offer, get input from deliver/limbo and output to next account or
// deliver for next offers.
//
// <-- node.saFwdDeliver: For calcNodeAccountFwd to know how much went through
// --> node.saRevDeliver: Do not exceed.

TER calcNodeDeliverFwd (
    RippleCalc& rippleCalc,
    const unsigned int nodeIndex,          // 0 < nodeIndex < lastNodeIndex
    PathState&         pathState,
    const bool         bMultiQuality,
    const uint160&     uInAccountID,   // --> Input owner's account.
    const STAmount&    saInReq,        // --> Amount to deliver.
    STAmount&          saInAct,        // <-- Amount delivered, this invokation.
    STAmount&          saInFees)       // <-- Fees charged, this invokation.
{
    TER errorCode   = tesSUCCESS;

    PathState::Node& previousNode = pathState.vpnNodes[nodeIndex - 1];
    PathState::Node& node = pathState.vpnNodes[nodeIndex];
    PathState::Node& nextNode = pathState.vpnNodes[nodeIndex + 1];

    const uint160&  nextAccountID   = nextNode.uAccountID;
    const uint160&  uCurCurrencyID  = node.uCurrencyID;
    const uint160&  uCurIssuerID    = node.uIssuerID;
    uint256 const&  uOfferIndex     = node.uOfferIndex;
    const uint160&  uPrvCurrencyID  = previousNode.uCurrencyID;
    const uint160&  uPrvIssuerID    = previousNode.uIssuerID;
    const STAmount& saInTransRate   = previousNode.saTransferRate;
    const STAmount& saCurDeliverMax = node.saRevDeliver;
    // Don't deliver more than wanted.

    STAmount&       saCurDeliverAct = node.saFwdDeliver;
    // Zeroed in reverse pass.

    uint256&        uDirectTip      = node.uDirectTip;
    bool&           bDirectRestart  = node.bDirectRestart;

    if (bMultiQuality)
        uDirectTip      = 0;                        // Restart book searching.
    else
        bDirectRestart  = true;                     // Restart at same quality.

    saInAct.clear (saInReq);
    saInFees.clear (saInReq);

    int loopCount = 0;

    // XXX Perhaps make sure do not exceed saCurDeliverMax as another way to
    // stop?
    while (errorCode == tesSUCCESS && saInAct + saInFees < saInReq)
    {
        // Did not spend all inbound deliver funds.
        if (++loopCount > CALC_NODE_DELIVER_MAX_LOOPS)
        {
            WriteLog (lsWARNING, RippleCalc)
                << "calcNodeDeliverFwd: max loops cndf";
            return rippleCalc.mOpenLedger ? telFAILED_PROCESSING : tecFAILED_PROCESSING;
        }

        // Determine values for pass to adjust saInAct, saInFees, and
        // saCurDeliverAct.
        errorCode   = calcNodeAdvance (
            rippleCalc,
            nodeIndex, pathState, bMultiQuality || saInAct == zero, false);
        // If needed, advance to next funded offer.

        if (errorCode != tesSUCCESS)
        {
            nothing ();
        }
        else if (!uOfferIndex)
        {
            WriteLog (lsWARNING, RippleCalc)
                << "calcNodeDeliverFwd: INTERNAL ERROR: Ran out of offers.";
            return rippleCalc.mOpenLedger ? telFAILED_PROCESSING : tecFAILED_PROCESSING;
        }
        else if (errorCode == tesSUCCESS)
        {
            // Doesn't charge input. Input funds are in limbo.
            bool&           bEntryAdvance   = node.bEntryAdvance;
            STAmount&       saOfrRate       = node.saOfrRate;
            uint256&        uOfferIndex     = node.uOfferIndex;
            SLE::pointer&   sleOffer        = node.sleOffer;
            const uint160&  uOfrOwnerID     = node.uOfrOwnerID;
            bool&           bFundsDirty     = node.bFundsDirty;
            STAmount&       saOfferFunds    = node.saOfferFunds;
            STAmount&       saTakerPays     = node.saTakerPays;
            STAmount&       saTakerGets     = node.saTakerGets;

            const STAmount  saInFeeRate
                = !uPrvCurrencyID                   // XRP.
                  || uInAccountID == uPrvIssuerID   // Sender is issuer.
                  || uOfrOwnerID == uPrvIssuerID    // Reciever is issuer.
                  ? saOne                           // No fee.
                  : saInTransRate;                  // Transfer rate of issuer.

            // First calculate assuming no output fees: saInPassAct,
            // saInPassFees, saOutPassAct.

            // Offer maximum out - limited by funds with out fees.
            STAmount    saOutFunded     = std::min (saOfferFunds, saTakerGets);

            // Offer maximum out - limit by most to deliver.
            STAmount    saOutPassFunded = std::min (
                saOutFunded, saCurDeliverMax - saCurDeliverAct);

            // Offer maximum in - Limited by by payout.
            STAmount    saInFunded      = STAmount::mulRound (
                saOutPassFunded, saOfrRate, saTakerPays, true);

            // Offer maximum in with fees.
            STAmount    saInTotal       = STAmount::mulRound (
                saInFunded, saInFeeRate, true);
            STAmount    saInRemaining   = saInReq - saInAct - saInFees;

            if (saInRemaining < zero)
                saInRemaining.clear();

            // In limited by remaining.
            STAmount    saInSum = std::min (saInTotal, saInRemaining);

            // In without fees.
            STAmount    saInPassAct = std::min (
                saTakerPays, STAmount::divRound (saInSum, saInFeeRate, true));

            // Out limited by in remaining.
            auto outPass = STAmount::divRound (
                saInPassAct, saOfrRate, saTakerGets, true);
            STAmount    saOutPassMax    = std::min (saOutPassFunded, outPass);

            STAmount    saInPassFeesMax = saInSum - saInPassAct;

            // Will be determined by next node.
            STAmount    saOutPassAct;

            // Will be determined by adjusted saInPassAct.
            STAmount    saInPassFees;

            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeDeliverFwd:"
                << " nodeIndex=" << nodeIndex
                << " saOutFunded=" << saOutFunded
                << " saOutPassFunded=" << saOutPassFunded
                << " saOfferFunds=" << saOfferFunds
                << " saTakerGets=" << saTakerGets
                << " saInReq=" << saInReq
                << " saInAct=" << saInAct
                << " saInFees=" << saInFees
                << " saInFunded=" << saInFunded
                << " saInTotal=" << saInTotal
                << " saInSum=" << saInSum
                << " saInPassAct=" << saInPassAct
                << " saOutPassMax=" << saOutPassMax;

            // FIXME: We remove an offer if WE didn't want anything out of it?
            if (!saTakerPays || saInSum <= zero)
            {
                WriteLog (lsDEBUG, RippleCalc)
                    << "calcNodeDeliverFwd: Microscopic offer unfunded.";

                // After math offer is effectively unfunded.
                pathState.vUnfundedBecame.push_back (uOfferIndex);
                bEntryAdvance   = true;
                continue;
            }
            else if (!saInFunded)
            {
                // Previous check should catch this.
                WriteLog (lsWARNING, RippleCalc)
                    << "calcNodeDeliverFwd: UNREACHABLE REACHED";

                // After math offer is effectively unfunded.
                pathState.vUnfundedBecame.push_back (uOfferIndex);
                bEntryAdvance   = true;
                continue;
            }
            else if (!!nextAccountID)
            {
                // ? --> OFFER --> account
                // Input fees: vary based upon the consumed offer's owner.
                // Output fees: none as XRP or the destination account is the
                // issuer.

                saOutPassAct    = saOutPassMax;
                saInPassFees    = saInPassFeesMax;

                WriteLog (lsTRACE, RippleCalc)
                    << "calcNodeDeliverFwd: ? --> OFFER --> account:"
                    << " uOfrOwnerID="
                    << RippleAddress::createHumanAccountID (uOfrOwnerID)
                    << " nextAccountID="
                    << RippleAddress::createHumanAccountID (nextAccountID)
                    << " saOutPassAct=" << saOutPassAct
                    << " saOutFunded=%s" << saOutFunded;

                // Output: Debit offer owner, send XRP or non-XPR to next
                // account.
                errorCode   = rippleCalc.mActiveLedger.accountSend (
                    uOfrOwnerID, nextAccountID, saOutPassAct);

                if (errorCode != tesSUCCESS)
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
                STAmount    saOutPassFees;

                // Output fees vary as the next nodes offer owners may vary.
                // Therefore, immediately push through output for current offer.
                errorCode   = calcNodeDeliverFwd (
                    rippleCalc,
                    nodeIndex + 1,
                    pathState,
                    bMultiQuality,
                    uOfrOwnerID,        // --> Current holder.
                    saOutPassMax,       // --> Amount available.
                    saOutPassAct,       // <-- Amount delivered.
                    saOutPassFees);     // <-- Fees charged.

                if (errorCode != tesSUCCESS)
                    break;

                if (saOutPassAct == saOutPassMax)
                {
                    // No fees and entire output amount.

                    saInPassFees    = saInPassFeesMax;
                }
                else
                {
                    // Fraction of output amount.
                    // Output fees are paid by offer owner and not passed to
                    // previous.

                    assert (saOutPassAct < saOutPassMax);
                    auto inPassAct = STAmount::mulRound (
                        saOutPassAct, saOfrRate, saInReq, true);
                    saInPassAct = std::min (saTakerPays, inPassAct);
                    auto inPassFees = STAmount::mulRound (
                        saInPassAct, saInFeeRate, true);
                    saInPassFees    = std::min (saInPassFeesMax, inPassFees);
                }

                // Do outbound debiting.
                // Send to issuer/limbo total amount including fees (issuer gets
                // fees).
                auto id = !!uCurCurrencyID ? uCurIssuerID : ACCOUNT_XRP;
                auto outPassTotal = saOutPassAct + saOutPassFees;
                rippleCalc.mActiveLedger.accountSend (uOfrOwnerID, id, outPassTotal);

                WriteLog (lsTRACE, RippleCalc)
                    << "calcNodeDeliverFwd: ? --> OFFER --> offer:"
                    << " saOutPassAct=" << saOutPassAct
                    << " saOutPassFees=" << saOutPassFees;
            }

            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeDeliverFwd: "
                << " nodeIndex=" << nodeIndex
                << " saTakerGets=" << saTakerGets
                << " saTakerPays=" << saTakerPays
                << " saInPassAct=" << saInPassAct
                << " saInPassFees=" << saInPassFees
                << " saOutPassAct=" << saOutPassAct
                << " saOutFunded=" << saOutFunded;

            // Funds were spent.
            bFundsDirty     = true;

            // Do inbound crediting.
            //
            // Credit offer owner from in issuer/limbo (input transfer fees left
            // with owner).  Don't attempt to have someone credit themselves, it
            // is redundant.
            if (!uPrvCurrencyID                 // Always credit XRP from limbo.
                || uInAccountID != uOfrOwnerID) // Never send non-XRP to the
                                                // same account.
            {
                auto id = !!uPrvCurrencyID ? uInAccountID : ACCOUNT_XRP;
                errorCode = rippleCalc.mActiveLedger.accountSend (
                    id, uOfrOwnerID, saInPassAct);

                if (errorCode != tesSUCCESS)
                    break;
            }

            // Adjust offer.
            //
            // Fees are considered paid from a seperate budget and are not named
            // in the offer.
            STAmount    saTakerGetsNew  = saTakerGets - saOutPassAct;
            STAmount    saTakerPaysNew  = saTakerPays - saInPassAct;

            if (saTakerPaysNew < zero || saTakerGetsNew < zero)
            {
                WriteLog (lsWARNING, RippleCalc)
                    << "calcNodeDeliverFwd: NEGATIVE:"
                    << " saTakerPaysNew=" << saTakerPaysNew
                    << " saTakerGetsNew=" << saTakerGetsNew;

                // If mOpenLedger, then ledger is not final, can vote no.
                errorCode   = rippleCalc.mOpenLedger
                              ? telFAILED_PROCESSING                                                          : tecFAILED_PROCESSING;
                break;
            }

            sleOffer->setFieldAmount (sfTakerGets, saTakerGetsNew);
            sleOffer->setFieldAmount (sfTakerPays, saTakerPaysNew);

            rippleCalc.mActiveLedger.entryModify (sleOffer);

            if (saOutPassAct == saOutFunded || saTakerGetsNew == zero)
            {
                // Offer became unfunded.

                WriteLog (lsWARNING, RippleCalc)
                    << "calcNodeDeliverFwd: unfunded:"
                    << " saOutPassAct=" << saOutPassAct
                    << " saOutFunded=" << saOutFunded;

                pathState.vUnfundedBecame.push_back (uOfferIndex);
                bEntryAdvance   = true;
            }
            else
            {
                CondLog (saOutPassAct >= saOutFunded, lsWARNING, RippleCalc)
                    << "calcNodeDeliverFwd: TOO MUCH:"
                    << " saOutPassAct=" << saOutPassAct
                    << " saOutFunded=" << saOutFunded;

                assert (saOutPassAct < saOutFunded);
            }

            saInAct         += saInPassAct;
            saInFees        += saInPassFees;

            // Adjust amount available to next node.
            saCurDeliverAct = std::min (saCurDeliverMax,
                                        saCurDeliverAct + saOutPassAct);
        }
    }

    WriteLog (lsTRACE, RippleCalc)
        << "calcNodeDeliverFwd<"
        << " nodeIndex=" << nodeIndex
        << " saInAct=" << saInAct
        << " saInFees=" << saInFees;

    return errorCode;
}

} // ripple
