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

TER RippleCalc::calcNodeAccountRev (
    const unsigned int uNode, PathState& psCur, const bool bMultiQuality)
{
    TER                 terResult       = tesSUCCESS;
    const unsigned int  uLast           = psCur.vpnNodes.size () - 1;

    std::uint64_t           uRateMax        = 0;

    PathState::Node& pnPrv = psCur.vpnNodes[uNode ? uNode - 1 : 0];
    PathState::Node& pnCur = psCur.vpnNodes[uNode];
    PathState::Node& pnNxt = psCur.vpnNodes[uNode == uLast ? uLast : uNode + 1];

    // Current is allowed to redeem to next.
    const bool bPrvAccount = !uNode ||
        is_bit_set (pnPrv.uFlags, STPathElement::typeAccount);
    const bool bNxtAccount = uNode == uLast ||
        is_bit_set (pnNxt.uFlags, STPathElement::typeAccount);

    const uint160& uCurAccountID = pnCur.uAccountID;
    const uint160& uPrvAccountID = bPrvAccount ? pnPrv.uAccountID
        : uCurAccountID;
    const uint160& uNxtAccountID = bNxtAccount ? pnNxt.uAccountID
        : uCurAccountID;   // Offers are always issue.

    const uint160& uCurrencyID = pnCur.uCurrencyID;

    // XXX Don't look up quality for XRP
    const std::uint32_t uQualityIn = uNode
        ? mActiveLedger.rippleQualityIn (
            uCurAccountID, uPrvAccountID, uCurrencyID)
        : QUALITY_ONE;
    const std::uint32_t uQualityOut = (uNode != uLast)
        ? mActiveLedger.rippleQualityOut (
            uCurAccountID, uNxtAccountID, uCurrencyID)
        : QUALITY_ONE;

    // For bPrvAccount:
    // Previous account is owed.
    const STAmount saPrvOwed = (bPrvAccount && uNode)
        ? mActiveLedger.rippleOwed (uCurAccountID, uPrvAccountID, uCurrencyID)
        : STAmount (uCurrencyID, uCurAccountID);

    // Previous account may owe.
    const STAmount saPrvLimit = (bPrvAccount && uNode)
        ? mActiveLedger.rippleLimit (uCurAccountID, uPrvAccountID, uCurrencyID)
        : STAmount (uCurrencyID, uCurAccountID);

    // Next account is owed.
    const STAmount saNxtOwed = (bNxtAccount && uNode != uLast)
        ? mActiveLedger.rippleOwed (uCurAccountID, uNxtAccountID, uCurrencyID)
        : STAmount (uCurrencyID, uCurAccountID);

    WriteLog (lsTRACE, RippleCalc)
        << "calcNodeAccountRev>"
        << " uNode=%d/%d" << uNode << "/" << uLast
        << " uPrvAccountID="
        << RippleAddress::createHumanAccountID (uPrvAccountID)
        << " uCurAccountID="
        << RippleAddress::createHumanAccountID (uCurAccountID)
        << " uNxtAccountID="
        << RippleAddress::createHumanAccountID (uNxtAccountID)
        << " uCurrencyID="
        << STAmount::createHumanCurrency (uCurrencyID)
        << " uQualityIn=" << uQualityIn
        << " uQualityOut=" << uQualityOut
        << " saPrvOwed=" << saPrvOwed
        << " saPrvLimit=" << saPrvLimit;

    // Previous can redeem the owed IOUs it holds.
    const STAmount  saPrvRedeemReq  = (saPrvOwed > zero)
        ? saPrvOwed
        : STAmount (saPrvOwed.getCurrency (), saPrvOwed.getIssuer ());
    STAmount& saPrvRedeemAct = pnPrv.saRevRedeem;

    // Previous can issue up to limit minus whatever portion of limit already
    // used (not including redeemable amount).
    const STAmount  saPrvIssueReq = (saPrvOwed < zero)
        ? saPrvLimit + saPrvOwed : saPrvLimit;
    STAmount& saPrvIssueAct = pnPrv.saRevIssue;

    // For !bPrvAccount
    auto deliverCurrency = pnPrv.saRevDeliver.getCurrency ();
    const STAmount  saPrvDeliverReq (
        deliverCurrency, pnPrv.saRevDeliver.getIssuer (), -1);  // Unlimited.
    STAmount&       saPrvDeliverAct = pnPrv.saRevDeliver;

    // For bNxtAccount
    const STAmount& saCurRedeemReq  = pnCur.saRevRedeem;
    STAmount saCurRedeemAct (
        saCurRedeemReq.getCurrency (), saCurRedeemReq.getIssuer ());

    const STAmount& saCurIssueReq   = pnCur.saRevIssue;
    // Track progress.
    STAmount saCurIssueAct (
        saCurIssueReq.getCurrency (), saCurIssueReq.getIssuer ());

    // For !bNxtAccount
    const STAmount& saCurDeliverReq = pnCur.saRevDeliver;
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

    WriteLog (lsTRACE, RippleCalc) << psCur.getJson ();

    // Current redeem req can't be more than IOUs on hand.
    assert (!saCurRedeemReq || (-saNxtOwed) >= saCurRedeemReq);
    assert (!saCurIssueReq  // If not issuing, fine.
            || saNxtOwed >= zero
            // saNxtOwed >= 0: Sender not holding next IOUs, saNxtOwed < 0:
            // Sender holding next IOUs.
            || -saNxtOwed == saCurRedeemReq);
    // If issue req, then redeem req must consume all owed.

    if (!uNode)
    {
        // ^ --> ACCOUNT -->  account|offer
        // Nothing to do, there is no previous to adjust.

        nothing ();
    }
    else if (bPrvAccount && bNxtAccount)
    {
        if (uNode == uLast)
        {
            // account --> ACCOUNT --> $
            // Overall deliverable.
             // If previous is an account, limit.
            const STAmount saCurWantedReq = std::min (
                psCur.saOutReq - psCur.saOutAct, saPrvLimit + saPrvOwed);
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
                    QUALITY_ONE,
                    mActiveLedger.rippleTransferRate (uCurAccountID),
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
    else if (bPrvAccount && !bNxtAccount)
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
                QUALITY_ONE, mActiveLedger.rippleTransferRate (uCurAccountID),
                saPrvRedeemReq, saCurDeliverReq, saPrvRedeemAct,
                saCurDeliverAct, uRateMax);
        }

        // issue -> deliver/issue
        if (saPrvRedeemReq == saPrvRedeemAct   // Previously redeemed all owed.
            && saCurDeliverReq != saCurDeliverAct)  // Still need some issued.
        {
            // Rate: quality in : 1.0
            calcNodeRipple (
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
    else if (!bPrvAccount && bNxtAccount)
    {
        if (uNode == uLast)
        {
            // offer --> ACCOUNT --> $
            // Previous is an offer, no limit: redeem own IOUs.
            const STAmount& saCurWantedReq  = psCur.saOutReq - psCur.saOutAct;
            STAmount saCurWantedAct (
                saCurWantedReq.getCurrency (), saCurWantedReq.getIssuer ());

            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeAccountRev: offer --> ACCOUNT --> $ :"
                << " saCurWantedReq:" << saCurWantedReq
                << " saOutAct:" << psCur.saOutAct
                << " saOutReq:" << psCur.saOutReq;

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
                    QUALITY_ONE,
                    mActiveLedger.rippleTransferRate (uCurAccountID),
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
            QUALITY_ONE, mActiveLedger.rippleTransferRate (uCurAccountID),
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
