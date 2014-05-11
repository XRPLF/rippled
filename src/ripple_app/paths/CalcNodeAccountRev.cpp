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

// Calculate saPrvRedeemReq, saPrvIssueReq, saPrvDeliver from saCur, based on
// required deliverable, propagate redeem, issue, and deliver requests to the
// previous node.
//
// Inflate amount requested by required fees.
// Reedems are limited based on IOUs previous has on hand.
// Issues are limited based on credit limits and amount owed.
//
// No account balance adjustments as we don't know how much is going to actually
// be pushed through yet.
//
// <-- tesSUCCESS or tecPATH_DRY

TER calcNodeAccountRev (
    RippleCalc& rippleCalc,
    const unsigned int nodeIndex, PathState& pathState,
    const bool bMultiQuality)
{
    TER terResult = tesSUCCESS;
    auto const lastNodeIndex = pathState.vpnNodes.size () - 1;
    auto const isFinalNode = (nodeIndex == lastNodeIndex);

    std::uint64_t uRateMax = 0;

    auto& previousNode = pathState.vpnNodes[nodeIndex ? nodeIndex - 1 : 0];
    auto& node = pathState.vpnNodes[nodeIndex];
    auto& nextNode = pathState.vpnNodes[isFinalNode ? lastNodeIndex : nodeIndex + 1];

    // Current is allowed to redeem to next.
    const bool previousNodeIsAccount = !nodeIndex ||
        is_bit_set (previousNode.uFlags, STPathElement::typeAccount);
    const bool nextNodeIsAccount = isFinalNode ||
        is_bit_set (nextNode.uFlags, STPathElement::typeAccount);

    const uint160& previousAccountID = previousNodeIsAccount
        ? previousNode.uAccountID : node.uAccountID;
    const uint160& nextAccountID = nextNodeIsAccount ? nextNode.uAccountID
        : node.uAccountID;   // Offers are always issue.

    // XXX Don't look up quality for XRP
    const std::uint32_t uQualityIn = nodeIndex
        ? rippleCalc.mActiveLedger.rippleQualityIn (
            node.uAccountID, previousAccountID, node.uCurrencyID)
        : QUALITY_ONE;
    const std::uint32_t uQualityOut = (nodeIndex != lastNodeIndex)
        ? rippleCalc.mActiveLedger.rippleQualityOut (
            node.uAccountID, nextAccountID, node.uCurrencyID)
        : QUALITY_ONE;

    // For previousNodeIsAccount:
    // Previous account is owed.
    const STAmount saPrvOwed = (previousNodeIsAccount && nodeIndex)
        ? rippleCalc.mActiveLedger.rippleOwed (
            node.uAccountID, previousAccountID, node.uCurrencyID)
        : STAmount (node.uCurrencyID, node.uAccountID);

    // Previous account may owe.
    const STAmount saPrvLimit = (previousNodeIsAccount && nodeIndex)
        ? rippleCalc.mActiveLedger.rippleLimit (
            node.uAccountID, previousAccountID, node.uCurrencyID)
        : STAmount (node.uCurrencyID, node.uAccountID);

    // Next account is owed.
    const STAmount saNxtOwed = (nextNodeIsAccount && nodeIndex != lastNodeIndex)
        ? rippleCalc.mActiveLedger.rippleOwed (
            node.uAccountID, nextAccountID, node.uCurrencyID)
        : STAmount (node.uCurrencyID, node.uAccountID);

    WriteLog (lsTRACE, RippleCalc)
        << "calcNodeAccountRev>"
        << " nodeIndex=%d/%d" << nodeIndex << "/" << lastNodeIndex
        << " previousAccountID="
        << RippleAddress::createHumanAccountID (previousAccountID)
        << " node.uAccountID="
        << RippleAddress::createHumanAccountID (node.uAccountID)
        << " nextAccountID="
        << RippleAddress::createHumanAccountID (nextAccountID)
        << " uCurrencyID="
        << STAmount::createHumanCurrency (node.uCurrencyID)
        << " uQualityIn=" << uQualityIn
        << " uQualityOut=" << uQualityOut
        << " saPrvOwed=" << saPrvOwed
        << " saPrvLimit=" << saPrvLimit;

    // Previous can redeem the owed IOUs it holds.
    const STAmount  saPrvRedeemReq  = (saPrvOwed > zero)
        ? saPrvOwed
        : STAmount (saPrvOwed.getCurrency (), saPrvOwed.getIssuer ());
    STAmount& saPrvRedeemAct = previousNode.saRevRedeem;

    // Previous can issue up to limit minus whatever portion of limit already
    // used (not including redeemable amount).
    const STAmount  saPrvIssueReq = (saPrvOwed < zero)
        ? saPrvLimit + saPrvOwed : saPrvLimit;
    STAmount& saPrvIssueAct = previousNode.saRevIssue;

    // For !previousNodeIsAccount
    auto deliverCurrency = previousNode.saRevDeliver.getCurrency ();
    const STAmount  saPrvDeliverReq (
        deliverCurrency, previousNode.saRevDeliver.getIssuer (), -1);
    // Unlimited.

    STAmount&       saPrvDeliverAct = previousNode.saRevDeliver;

    // For nextNodeIsAccount
    const STAmount& saCurRedeemReq  = node.saRevRedeem;
    STAmount saCurRedeemAct (
        saCurRedeemReq.getCurrency (), saCurRedeemReq.getIssuer ());

    const STAmount& saCurIssueReq   = node.saRevIssue;
    // Track progress.
    STAmount saCurIssueAct (
        saCurIssueReq.getCurrency (), saCurIssueReq.getIssuer ());

    // For !nextNodeIsAccount
    const STAmount& saCurDeliverReq = node.saRevDeliver;
    STAmount saCurDeliverAct (
        saCurDeliverReq.getCurrency (), saCurDeliverReq.getIssuer ());

    WriteLog (lsTRACE, RippleCalc)
        << "calcNodeAccountRev:"
        << " saPrvRedeemReq:" << saPrvRedeemReq
        << " saPrvIssueReq:" << saPrvIssueReq
        << " saPrvDeliverAct:" << saPrvDeliverAct
        << " saPrvDeliverReq:" << saPrvDeliverReq
        << " saCurRedeemReq:" << saCurRedeemReq
        << " saCurIssueReq:" << saCurIssueReq
        << " saNxtOwed:" << saNxtOwed;

    WriteLog (lsTRACE, RippleCalc) << pathState.getJson ();

    // Current redeem req can't be more than IOUs on hand.
    assert (!saCurRedeemReq || (-saNxtOwed) >= saCurRedeemReq);
    assert (!saCurIssueReq  // If not issuing, fine.
            || saNxtOwed >= zero
            // saNxtOwed >= 0: Sender not holding next IOUs, saNxtOwed < 0:
            // Sender holding next IOUs.
            || -saNxtOwed == saCurRedeemReq);
    // If issue req, then redeem req must consume all owed.

    if (!nodeIndex)
    {
        // ^ --> ACCOUNT -->  account|offer
        // Nothing to do, there is no previous to adjust.

        nothing ();
    }
    else if (previousNodeIsAccount && nextNodeIsAccount)
    {
        if (isFinalNode)
        {
            // account --> ACCOUNT --> $
            // Overall deliverable.
             // If previous is an account, limit.
            const STAmount saCurWantedReq = std::min (
                pathState.saOutReq - pathState.saOutAct, saPrvLimit + saPrvOwed);
            STAmount        saCurWantedAct (
                saCurWantedReq.getCurrency (), saCurWantedReq.getIssuer ());

            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeAccountRev: account --> ACCOUNT --> $ :"
                << " saCurWantedReq=" << saCurWantedReq;

            // Calculate redeem
            if (saPrvRedeemReq) // Previous has IOUs to redeem.
            {
                // Redeem at 1:1

                saCurWantedAct = std::min (saPrvRedeemReq, saCurWantedReq);
                saPrvRedeemAct = saCurWantedAct;

                uRateMax = STAmount::uRateOne;

                WriteLog (lsTRACE, RippleCalc)
                    << "calcNodeAccountRev: Redeem at 1:1"
                    << " saPrvRedeemReq=" << saPrvRedeemReq
                    << " (available) saPrvRedeemAct=" << saPrvRedeemAct
                    << " uRateMax="
                    << STAmount::saFromRate (uRateMax).getText ();
            }
            else
            {
                saPrvRedeemAct.clear (saPrvRedeemReq);
            }

            // Calculate issuing.
            saPrvIssueAct.clear (saPrvIssueReq);

            if (saCurWantedReq != saCurWantedAct // Need more.
                && saPrvIssueReq)  // Will accept IOUs from prevous.
            {
                // Rate: quality in : 1.0

                // If we previously redeemed and this has a poorer rate, this
                // won't be included the current increment.
                calcNodeRipple (
                    rippleCalc,
                    uQualityIn, QUALITY_ONE, saPrvIssueReq, saCurWantedReq,
                    saPrvIssueAct, saCurWantedAct, uRateMax);

                WriteLog (lsTRACE, RippleCalc)
                    << "calcNodeAccountRev: Issuing: Rate: quality in : 1.0"
                    << " saPrvIssueAct:" << saPrvIssueAct
                    << " saCurWantedAct:" << saCurWantedAct;
            }

            if (!saCurWantedAct)
            {
                // Must have processed something.
                terResult   = tecPATH_DRY;
            }
        }
        else
        {
            // ^|account --> ACCOUNT --> account
            saPrvRedeemAct.clear (saPrvRedeemReq);
            saPrvIssueAct.clear (saPrvIssueReq);

            // redeem (part 1) -> redeem
            if (saCurRedeemReq      // Next wants IOUs redeemed.
                && saPrvRedeemReq)  // Previous has IOUs to redeem.
            {
                // Rate : 1.0 : quality out
                calcNodeRipple (
                    rippleCalc,
                    QUALITY_ONE, uQualityOut, saPrvRedeemReq, saCurRedeemReq,
                    saPrvRedeemAct, saCurRedeemAct, uRateMax);

                WriteLog (lsTRACE, RippleCalc)
                    << "calcNodeAccountRev: Rate : 1.0 : quality out"
                    << " saPrvRedeemAct:" << saPrvRedeemAct
                    << " saCurRedeemAct:" << saCurRedeemAct;
            }

            // issue (part 1) -> redeem
            if (saCurRedeemReq != saCurRedeemAct
                // Next wants more IOUs redeemed.
                && saPrvRedeemAct == saPrvRedeemReq)
                // Previous has no IOUs to redeem remaining.
            {
                // Rate: quality in : quality out
                calcNodeRipple (
                    rippleCalc,
                    uQualityIn, uQualityOut, saPrvIssueReq, saCurRedeemReq,
                    saPrvIssueAct, saCurRedeemAct, uRateMax);

                WriteLog (lsTRACE, RippleCalc)
                    << "calcNodeAccountRev: Rate: quality in : quality out:"
                    << " saPrvIssueAct:" << saPrvIssueAct
                    << " saCurRedeemAct:" << saCurRedeemAct;
            }

            // redeem (part 2) -> issue.
            if (saCurIssueReq   // Next wants IOUs issued.
                && saCurRedeemAct == saCurRedeemReq
                // Can only issue if completed redeeming.
                && saPrvRedeemAct != saPrvRedeemReq)
                // Did not complete redeeming previous IOUs.
            {
                // Rate : 1.0 : transfer_rate
                calcNodeRipple (
                    rippleCalc,
                    QUALITY_ONE,
                    rippleCalc.mActiveLedger.rippleTransferRate (node.uAccountID),
                    saPrvRedeemReq, saCurIssueReq, saPrvRedeemAct,
                    saCurIssueAct, uRateMax);

                WriteLog (lsDEBUG, RippleCalc)
                    << "calcNodeAccountRev: Rate : 1.0 : transfer_rate:"
                    << " saPrvRedeemAct:" << saPrvRedeemAct
                    << " saCurIssueAct:" << saCurIssueAct;
            }

            // issue (part 2) -> issue
            if (saCurIssueReq != saCurIssueAct
                // Need wants more IOUs issued.
                && saCurRedeemAct == saCurRedeemReq
                // Can only issue if completed redeeming.
                && saPrvRedeemReq == saPrvRedeemAct
                // Previously redeemed all owed IOUs.
                && saPrvIssueReq)
                // Previous can issue.
            {
                // Rate: quality in : 1.0
                calcNodeRipple (
                    rippleCalc,
                    uQualityIn, QUALITY_ONE, saPrvIssueReq, saCurIssueReq,
                    saPrvIssueAct, saCurIssueAct, uRateMax);

                WriteLog (lsTRACE, RippleCalc)
                    << "calcNodeAccountRev: Rate: quality in : 1.0:"
                    << " saPrvIssueAct:" << saPrvIssueAct
                    << " saCurIssueAct:" << saCurIssueAct;
            }

            if (!saCurRedeemAct && !saCurIssueAct)
            {
                // Did not make progress.
                terResult   = tecPATH_DRY;
            }

            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeAccountRev: ^|account --> ACCOUNT --> account :"
                << " saCurRedeemReq:" << saCurRedeemReq
                << " saCurIssueReq:" << saCurIssueReq
                << " saPrvOwed:" << saPrvOwed
                << " saCurRedeemAct:" << saCurRedeemAct
                << " saCurIssueAct:" << saCurIssueAct;
        }
    }
    else if (previousNodeIsAccount && !nextNodeIsAccount)
    {
        // account --> ACCOUNT --> offer
        // Note: deliver is always issue as ACCOUNT is the issuer for the offer
        // input.
        WriteLog (lsTRACE, RippleCalc)
            << "calcNodeAccountRev: account --> ACCOUNT --> offer";

        saPrvRedeemAct.clear (saPrvRedeemReq);
        saPrvIssueAct.clear (saPrvIssueReq);

        // redeem -> deliver/issue.
        if (saPrvOwed > zero                    // Previous has IOUs to redeem.
            && saCurDeliverReq)                 // Need some issued.
        {
            // Rate : 1.0 : transfer_rate
            calcNodeRipple (
                rippleCalc, QUALITY_ONE,
                rippleCalc.mActiveLedger.rippleTransferRate (node.uAccountID),
                saPrvRedeemReq, saCurDeliverReq, saPrvRedeemAct,
                saCurDeliverAct, uRateMax);
        }

        // issue -> deliver/issue
        if (saPrvRedeemReq == saPrvRedeemAct   // Previously redeemed all owed.
            && saCurDeliverReq != saCurDeliverAct)  // Still need some issued.
        {
            // Rate: quality in : 1.0
            calcNodeRipple (
                rippleCalc,
                uQualityIn, QUALITY_ONE, saPrvIssueReq, saCurDeliverReq,
                saPrvIssueAct, saCurDeliverAct, uRateMax);
        }

        if (!saCurDeliverAct)
        {
            // Must want something.
            terResult   = tecPATH_DRY;
        }

        WriteLog (lsTRACE, RippleCalc)
            << "calcNodeAccountRev: "
            << " saCurDeliverReq:" << saCurDeliverReq
            << " saCurDeliverAct:" << saCurDeliverAct
            << " saPrvOwed:" << saPrvOwed;
    }
    else if (!previousNodeIsAccount && nextNodeIsAccount)
    {
        if (isFinalNode)
        {
            // offer --> ACCOUNT --> $
            // Previous is an offer, no limit: redeem own IOUs.
            const STAmount& saCurWantedReq  = pathState.saOutReq - pathState.saOutAct;
            STAmount saCurWantedAct (
                saCurWantedReq.getCurrency (), saCurWantedReq.getIssuer ());

            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeAccountRev: offer --> ACCOUNT --> $ :"
                << " saCurWantedReq:" << saCurWantedReq
                << " saOutAct:" << pathState.saOutAct
                << " saOutReq:" << pathState.saOutReq;

            if (saCurWantedReq <= zero)
            {
                // TEMPORARY emergency fix
                WriteLog (lsFATAL, RippleCalc) << "CurWantReq was not positive";
                return tefEXCEPTION;
            }

            assert (saCurWantedReq > zero); // FIXME: We got one of these
            // TODO(tom): can only be a race condition if true!

            // Rate: quality in : 1.0
            calcNodeRipple (
                rippleCalc,
                uQualityIn, QUALITY_ONE, saPrvDeliverReq, saCurWantedReq,
                saPrvDeliverAct, saCurWantedAct, uRateMax);

            if (!saCurWantedAct)
            {
                // Must have processed something.
                terResult   = tecPATH_DRY;
            }

            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeAccountRev:"
                << " saPrvDeliverAct:" << saPrvDeliverAct
                << " saPrvDeliverReq:" << saPrvDeliverReq
                << " saCurWantedAct:" << saCurWantedAct
                << " saCurWantedReq:" << saCurWantedReq;
        }
        else
        {
            // offer --> ACCOUNT --> account
            // Note: offer is always delivering(redeeming) as account is issuer.
            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeAccountRev: offer --> ACCOUNT --> account :"
                << " saCurRedeemReq:" << saCurRedeemReq
                << " saCurIssueReq:" << saCurIssueReq;

            // deliver -> redeem
            if (saCurRedeemReq)  // Next wants us to redeem.
            {
                // Rate : 1.0 : quality out
                calcNodeRipple (
                    rippleCalc,
                    QUALITY_ONE, uQualityOut, saPrvDeliverReq, saCurRedeemReq,
                    saPrvDeliverAct, saCurRedeemAct, uRateMax);
            }

            // deliver -> issue.
            if (saCurRedeemReq == saCurRedeemAct
                // Can only issue if previously redeemed all.
                && saCurIssueReq)
                // Need some issued.
            {
                // Rate : 1.0 : transfer_rate
                calcNodeRipple (
                    rippleCalc,
                    QUALITY_ONE,
                    rippleCalc.mActiveLedger.rippleTransferRate (node.uAccountID),
                    saPrvDeliverReq, saCurIssueReq, saPrvDeliverAct,
                    saCurIssueAct, uRateMax);
            }

            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeAccountRev:"
                << " saCurRedeemAct:" << saCurRedeemAct
                << " saCurRedeemReq:" << saCurRedeemReq
                << " saPrvDeliverAct:" << saPrvDeliverAct
                << " saCurIssueReq:" << saCurIssueReq;

            if (!saPrvDeliverAct)
            {
                // Must want something.
                terResult   = tecPATH_DRY;
            }
        }
    }
    else
    {
        // offer --> ACCOUNT --> offer
        // deliver/redeem -> deliver/issue.
        WriteLog (lsTRACE, RippleCalc)
            << "calcNodeAccountRev: offer --> ACCOUNT --> offer";

        // Rate : 1.0 : transfer_rate
        calcNodeRipple (
            rippleCalc,
            QUALITY_ONE,
            rippleCalc.mActiveLedger.rippleTransferRate (node.uAccountID),
            saPrvDeliverReq, saCurDeliverReq, saPrvDeliverAct,
            saCurDeliverAct, uRateMax);

        if (!saCurDeliverAct)
        {
            // Must want something.
            terResult   = tecPATH_DRY;
        }
    }

    return terResult;
}

} // ripple
