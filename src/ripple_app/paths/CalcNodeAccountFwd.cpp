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

// The reverse pass has been narrowing by credit available and inflating by fees
// as it worked backwards.  Now, for the current account node, take the actual
// amount from previous and adjust forward balances.
//
// Perform balance adjustments between previous and current node.
// - The previous node: specifies what to push through to current.
// - All of previous output is consumed.
//
// Then, compute current node's output for next node.
// - Current node: specify what to push through to next.
// - Output to next node is computed as input minus quality or transfer fee.
// - If next node is an offer and output is non-XRP then we are the issuer and
//   do not need to push funds.
// - If next node is an offer and output is XRP then we need to deliver funds to
//   limbo.
TER RippleCalc::calcNodeAccountFwd (
    const unsigned int uNode,   // 0 <= uNode <= uLast
    PathState& psCur,
    const bool bMultiQuality)
{
    TER                 terResult   = tesSUCCESS;
    const unsigned int  uLast       = psCur.vpnNodes.size () - 1;

    std::uint64_t       uRateMax    = 0;

    PathState::Node& pnPrv = psCur.vpnNodes[uNode ? uNode - 1 : 0];
    PathState::Node& pnCur = psCur.vpnNodes[uNode];
    PathState::Node& pnNxt = psCur.vpnNodes[uNode == uLast ? uLast : uNode + 1];

    const bool bPrvAccount
        = is_bit_set (pnPrv.uFlags, STPathElement::typeAccount);
    const bool bNxtAccount
        = is_bit_set (pnNxt.uFlags, STPathElement::typeAccount);

    const uint160& uCurAccountID = pnCur.uAccountID;
    const uint160& uPrvAccountID
        = bPrvAccount ? pnPrv.uAccountID : uCurAccountID;
    // Offers are always issue.
    const uint160& uNxtAccountID
            = bNxtAccount ? pnNxt.uAccountID : uCurAccountID;

    const uint160&  uCurrencyID     = pnCur.uCurrencyID;

    std::uint32_t uQualityIn = uNode
        ? mActiveLedger.rippleQualityIn (
            uCurAccountID, uPrvAccountID, uCurrencyID)
        : QUALITY_ONE;
    std::uint32_t  uQualityOut = (uNode == uLast)
        ? mActiveLedger.rippleQualityOut (
            uCurAccountID, uNxtAccountID, uCurrencyID)
        : QUALITY_ONE;

    // When looking backward (prv) for req we care about what we just
    // calculated: use fwd.
    // When looking forward (cur) for req we care about what was desired: use
    // rev.

    // For bNxtAccount
    const STAmount& saPrvRedeemReq  = pnPrv.saFwdRedeem;
    STAmount saPrvRedeemAct (
        saPrvRedeemReq.getCurrency (), saPrvRedeemReq.getIssuer ());

    const STAmount& saPrvIssueReq   = pnPrv.saFwdIssue;
    STAmount saPrvIssueAct (
        saPrvIssueReq.getCurrency (), saPrvIssueReq.getIssuer ());

    // For !bPrvAccount
    const STAmount& saPrvDeliverReq = pnPrv.saFwdDeliver;
    STAmount saPrvDeliverAct (
        saPrvDeliverReq.getCurrency (), saPrvDeliverReq.getIssuer ());

    // For bNxtAccount
    const STAmount& saCurRedeemReq  = pnCur.saRevRedeem;
    STAmount& saCurRedeemAct  = pnCur.saFwdRedeem;

    const STAmount& saCurIssueReq   = pnCur.saRevIssue;
    STAmount& saCurIssueAct   = pnCur.saFwdIssue;

    // For !bNxtAccount
    const STAmount& saCurDeliverReq = pnCur.saRevDeliver;
    STAmount& saCurDeliverAct = pnCur.saFwdDeliver;

    // For !uNode
    // Report how much pass sends.
    STAmount& saCurSendMaxPass = psCur.saInPass;

    WriteLog (lsTRACE, RippleCalc)
        << "calcNodeAccountFwd> "
        << "uNode=" << uNode << "/" << uLast
        << " saPrvRedeemReq:" << saPrvRedeemReq
        << " saPrvIssueReq:" << saPrvIssueReq
        << " saPrvDeliverReq:" << saPrvDeliverReq
        << " saCurRedeemReq:" << saCurRedeemReq
        << " saCurIssueReq:" << saCurIssueReq
        << " saCurDeliverReq:" << saCurDeliverReq;

    // Ripple through account.

    if (bPrvAccount && bNxtAccount)
    {
        // Next is an account, must be rippling.

        if (!uNode)
        {
            // ^ --> ACCOUNT --> account

            // For the first node, calculate amount to ripple based on what is
            // available.
            saCurRedeemAct      = saCurRedeemReq;

            if (psCur.saInReq >= zero)
            {
                // Limit by send max.
                saCurRedeemAct = std::min (
                    saCurRedeemAct, psCur.saInReq - psCur.saInAct);
            }

            saCurSendMaxPass    = saCurRedeemAct;

            saCurIssueAct = saCurRedeemAct == saCurRedeemReq
                // Fully redeemed.
                ? saCurIssueReq : STAmount (saCurIssueReq);

            if (!!saCurIssueAct && psCur.saInReq >= zero)
            {
                // Limit by send max.
                saCurIssueAct = std::min (
                    saCurIssueAct,
                    psCur.saInReq - psCur.saInAct - saCurRedeemAct);
            }

            saCurSendMaxPass += saCurIssueAct;

            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeAccountFwd: ^ --> ACCOUNT --> account :"
                << " saInReq=" << psCur.saInReq
                << " saInAct=" << psCur.saInAct
                << " saCurRedeemAct:" << saCurRedeemAct
                << " saCurIssueReq:" << saCurIssueReq
                << " saCurIssueAct:" << saCurIssueAct
                << " saCurSendMaxPass:" << saCurSendMaxPass;
        }
        else if (uNode == uLast)
        {
            // account --> ACCOUNT --> $
            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeAccountFwd: account --> ACCOUNT --> $ :"
                << " uPrvAccountID="
                << RippleAddress::createHumanAccountID (uPrvAccountID)
                << " uCurAccountID="
                << RippleAddress::createHumanAccountID (uCurAccountID)
                << " saPrvRedeemReq:" << saPrvRedeemReq
                << " saPrvIssueReq:" << saPrvIssueReq;

            // Last node. Accept all funds. Calculate amount actually to credit.

            STAmount& saCurReceive = psCur.saOutPass;

            STAmount saIssueCrd = uQualityIn >= QUALITY_ONE
                    ? saPrvIssueReq  // No fee.
                    : STAmount::mulRound (
                          saPrvIssueReq,
                          STAmount (CURRENCY_ONE, ACCOUNT_ONE, uQualityIn, -9),
                          true); // Amount to credit.

            // Amount to credit. Credit for less than received as a surcharge.
            saCurReceive    = saPrvRedeemReq + saIssueCrd;

            if (saCurReceive)
            {
                // Actually receive.
                terResult = mActiveLedger.rippleCredit (
                    uPrvAccountID, uCurAccountID,
                    saPrvRedeemReq + saPrvIssueReq, false);
            }
            else
            {
                // After applying quality, total payment was microscopic.
                terResult   = tecPATH_DRY;
            }
        }
        else
        {
            // account --> ACCOUNT --> account
            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeAccountFwd: account --> ACCOUNT --> account";

            saCurRedeemAct.clear (saCurRedeemReq);
            saCurIssueAct.clear (saCurIssueReq);

            // Previous redeem part 1: redeem -> redeem
            if (saPrvRedeemReq && saCurRedeemReq)
                // Previous wants to redeem.
            {
                // Rate : 1.0 : quality out
                calcNodeRipple (
                    QUALITY_ONE, uQualityOut, saPrvRedeemReq, saCurRedeemReq,
                    saPrvRedeemAct, saCurRedeemAct, uRateMax);
            }

            // Previous issue part 1: issue -> redeem
            if (saPrvIssueReq != saPrvIssueAct
                // Previous wants to issue.
                && saCurRedeemReq != saCurRedeemAct)
                // Current has more to redeem to next.
            {
                // Rate: quality in : quality out
                calcNodeRipple (
                    uQualityIn, uQualityOut, saPrvIssueReq, saCurRedeemReq,
                    saPrvIssueAct, saCurRedeemAct, uRateMax);
            }

            // Previous redeem part 2: redeem -> issue.
            if (saPrvRedeemReq != saPrvRedeemAct
                // Previous still wants to redeem.
                && saCurRedeemReq == saCurRedeemAct
                // Current redeeming is done can issue.
                && saCurIssueReq)
                // Current wants to issue.
            {
                // Rate : 1.0 : transfer_rate
                calcNodeRipple (
                    QUALITY_ONE,
                    mActiveLedger.rippleTransferRate (uCurAccountID),
                    saPrvRedeemReq, saCurIssueReq, saPrvRedeemAct,
                    saCurIssueAct, uRateMax);
            }

            // Previous issue part 2 : issue -> issue
            if (saPrvIssueReq != saPrvIssueAct
                // Previous wants to issue.
                && saCurRedeemReq == saCurRedeemAct
                // Current redeeming is done can issue.
                && saCurIssueReq)
                // Current wants to issue.
            {
                // Rate: quality in : 1.0
                calcNodeRipple (
                    uQualityIn, QUALITY_ONE, saPrvIssueReq, saCurIssueReq,
                    saPrvIssueAct, saCurIssueAct, uRateMax);
            }

            STAmount saProvide = saCurRedeemAct + saCurIssueAct;

            // Adjust prv --> cur balance : take all inbound
            terResult   = saProvide
                ? mActiveLedger.rippleCredit (uPrvAccountID, uCurAccountID,
                                          saPrvRedeemReq + saPrvIssueReq, false)
                : tecPATH_DRY;
        }
    }
    else if (bPrvAccount && !bNxtAccount)
    {
        // Current account is issuer to next offer.
        // Determine deliver to offer amount.
        // Don't adjust outbound balances- keep funds with issuer as limbo.
        // If issuer hold's an offer owners inbound IOUs, there is no fee and
        // redeem/issue will transparently happen.

        if (uNode)
        {
            // Non-XRP, current node is the issuer.
            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeAccountFwd: account --> ACCOUNT --> offer";

            saCurDeliverAct.clear (saCurDeliverReq);

            // redeem -> issue/deliver.
            // Previous wants to redeem.
            // Current is issuing to an offer so leave funds in account as
            // "limbo".
            if (saPrvRedeemReq)
                // Previous wants to redeem.
            {
                // Rate : 1.0 : transfer_rate
                // XXX Is having the transfer rate here correct?
                calcNodeRipple (
                    QUALITY_ONE,
                    mActiveLedger.rippleTransferRate (uCurAccountID),
                    saPrvRedeemReq, saCurDeliverReq, saPrvRedeemAct,
                    saCurDeliverAct, uRateMax);
            }

            // issue -> issue/deliver
            if (saPrvRedeemReq == saPrvRedeemAct
                // Previous done redeeming: Previous has no IOUs.
                && saPrvIssueReq)
                // Previous wants to issue. To next must be ok.
            {
                // Rate: quality in : 1.0
                calcNodeRipple (
                    uQualityIn, QUALITY_ONE, saPrvIssueReq, saCurDeliverReq,
                    saPrvIssueAct, saCurDeliverAct, uRateMax);
            }

            // Adjust prv --> cur balance : take all inbound
            terResult   = saCurDeliverAct
                ? mActiveLedger.rippleCredit (uPrvAccountID, uCurAccountID,
                                          saPrvRedeemReq + saPrvIssueReq, false)
                : tecPATH_DRY;  // Didn't actually deliver anything.
        }
        else
        {
            // Delivering amount requested from downstream.
            saCurDeliverAct = saCurDeliverReq;

            // If limited, then limit by send max and available.
            if (psCur.saInReq >= zero)
            {
                // Limit by send max.
                saCurDeliverAct = std::min (saCurDeliverAct,
                                            psCur.saInReq - psCur.saInAct);

                // Limit XRP by available. No limit for non-XRP as issuer.
                if (uCurrencyID.isZero ())
                    saCurDeliverAct = std::min (
                        saCurDeliverAct,
                        mActiveLedger.accountHolds (uCurAccountID, CURRENCY_XRP,
                                                ACCOUNT_XRP));

            }

            // Record amount sent for pass.
            saCurSendMaxPass    = saCurDeliverAct;

            if (!saCurDeliverAct)
            {
                terResult   = tecPATH_DRY;
            }
            else if (!!uCurrencyID)
            {
                // Non-XRP, current node is the issuer.
                // We could be delivering to multiple accounts, so we don't know
                // which ripple balance will be adjusted.  Assume just issuing.

                WriteLog (lsTRACE, RippleCalc)
                    << "calcNodeAccountFwd: ^ --> ACCOUNT -- !XRP --> offer";

                // As the issuer, would only issue.
                // Don't need to actually deliver. As from delivering leave in
                // the issuer as limbo.
                nothing ();
            }
            else
            {
                WriteLog (lsTRACE, RippleCalc)
                    << "calcNodeAccountFwd: ^ --> ACCOUNT -- XRP --> offer";

                // Deliver XRP to limbo.
                terResult = mActiveLedger.accountSend (
                    uCurAccountID, ACCOUNT_XRP, saCurDeliverAct);
            }
        }
    }
    else if (!bPrvAccount && bNxtAccount)
    {
        if (uNode == uLast)
        {
            // offer --> ACCOUNT --> $
            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeAccountFwd: offer --> ACCOUNT --> $ : "
                << saPrvDeliverReq;

            STAmount& saCurReceive = psCur.saOutPass;

            // Amount to credit.
            saCurReceive    = saPrvDeliverReq;

            // No income balance adjustments necessary.  The paying side inside
            // the offer paid to this account.
        }
        else
        {
            // offer --> ACCOUNT --> account
            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeAccountFwd: offer --> ACCOUNT --> account";

            saCurRedeemAct.clear (saCurRedeemReq);
            saCurIssueAct.clear (saCurIssueReq);

            // deliver -> redeem
            if (saPrvDeliverReq && saCurRedeemReq)
                // Previous wants to deliver and can current redeem.
            {
                // Rate : 1.0 : quality out
                calcNodeRipple (
                    QUALITY_ONE, uQualityOut, saPrvDeliverReq, saCurRedeemReq,
                    saPrvDeliverAct, saCurRedeemAct, uRateMax);
            }

            // deliver -> issue
            // Wants to redeem and current would and can issue.
            if (saPrvDeliverReq != saPrvDeliverAct
                // Previous still wants to deliver.
                && saCurRedeemReq == saCurRedeemAct
                // Current has more to redeem to next.
                && saCurIssueReq)
                // Current wants issue.
            {
                // Rate : 1.0 : transfer_rate
                calcNodeRipple (
                    QUALITY_ONE,
                    mActiveLedger.rippleTransferRate (uCurAccountID),
                    saPrvDeliverReq, saCurIssueReq, saPrvDeliverAct,
                    saCurIssueAct, uRateMax);
            }

            // No income balance adjustments necessary.  The paying side inside
            // the offer paid and the next link will receive.
            STAmount saProvide = saCurRedeemAct + saCurIssueAct;

            if (!saProvide)
                terResult = tecPATH_DRY;
        }
    }
    else
    {
        // offer --> ACCOUNT --> offer
        // deliver/redeem -> deliver/issue.
        WriteLog (lsTRACE, RippleCalc)
            << "calcNodeAccountFwd: offer --> ACCOUNT --> offer";

        saCurDeliverAct.clear (saCurDeliverReq);

        if (saPrvDeliverReq
            // Previous wants to deliver
            && saCurIssueReq)
            // Current wants issue.
        {
            // Rate : 1.0 : transfer_rate
            calcNodeRipple (
                QUALITY_ONE, mActiveLedger.rippleTransferRate (uCurAccountID),
                saPrvDeliverReq, saCurDeliverReq, saPrvDeliverAct,
                saCurDeliverAct, uRateMax);
        }

        // No income balance adjustments necessary.  The paying side inside the
        // offer paid and the next link will receive.
        if (!saCurDeliverAct)
            terResult   = tecPATH_DRY;
    }

    return terResult;
}

} // ripple
