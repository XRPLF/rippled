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

#include <ripple_app/paths/Calculators.h>
#include <ripple_app/paths/RippleCalc.h>
#include <ripple_app/paths/Tuning.h>

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
TER calcNodeAccountFwd (
    RippleCalc& rippleCalc,
    const unsigned int nodeIndex,   // 0 <= nodeIndex <= lastNodeIndex
    PathState& pathState,
    const bool bMultiQuality)
{
    TER                 errorCode   = tesSUCCESS;
    const unsigned int  lastNodeIndex       = pathState.vpnNodes.size () - 1;

    std::uint64_t       uRateMax    = 0;

    auto& previousNode = pathState.vpnNodes[nodeIndex ? nodeIndex - 1 : 0];
    auto& node = pathState.vpnNodes[nodeIndex];
    auto& nextNode = pathState.vpnNodes[nodeIndex == lastNodeIndex ? lastNodeIndex : nodeIndex + 1];

    const bool previousNodeIsAccount
        = is_bit_set (previousNode.uFlags, STPathElement::typeAccount);
    const bool nextNodeIsAccount
        = is_bit_set (nextNode.uFlags, STPathElement::typeAccount);

    const uint160& previousAccountID
        = previousNodeIsAccount ? previousNode.uAccountID : node.uAccountID;
    // Offers are always issue.
    const uint160& nextAccountID
        = nextNodeIsAccount ? nextNode.uAccountID : node.uAccountID;

    std::uint32_t uQualityIn = nodeIndex
        ? rippleCalc.mActiveLedger.rippleQualityIn (
            node.uAccountID, previousAccountID, node.uCurrencyID)
        : QUALITY_ONE;
    std::uint32_t  uQualityOut = (nodeIndex == lastNodeIndex)
        ? rippleCalc.mActiveLedger.rippleQualityOut (
            node.uAccountID, nextAccountID, node.uCurrencyID)
        : QUALITY_ONE;

    // When looking backward (prv) for req we care about what we just
    // calculated: use fwd.
    // When looking forward (cur) for req we care about what was desired: use
    // rev.

    // For nextNodeIsAccount
    STAmount saPrvRedeemAct (
        previousNode.saFwdRedeem.getCurrency (),
        previousNode.saFwdRedeem.getIssuer ());

    STAmount saPrvIssueAct (
        previousNode.saFwdIssue.getCurrency (),
        previousNode.saFwdIssue.getIssuer ());

    // For !previousNodeIsAccount
    STAmount saPrvDeliverAct (
        previousNode.saFwdDeliver.getCurrency (),
        previousNode.saFwdDeliver.getIssuer ());

    WriteLog (lsTRACE, RippleCalc)
        << "calcNodeAccountFwd> "
        << "nodeIndex=" << nodeIndex << "/" << lastNodeIndex
        << " previousNode.saFwdRedeem:" << previousNode.saFwdRedeem
        << " saPrvIssueReq:" << previousNode.saFwdIssue
        << " previousNode.saFwdDeliver:" << previousNode.saFwdDeliver
        << " node.saRevRedeem:" << node.saRevRedeem
        << " node.saRevIssue:" << node.saRevIssue
        << " node.saRevDeliver:" << node.saRevDeliver;

    // Ripple through account.

    if (previousNodeIsAccount && nextNodeIsAccount)
    {
        // Next is an account, must be rippling.

        if (!nodeIndex)
        {
            // ^ --> ACCOUNT --> account

            // For the first node, calculate amount to ripple based on what is
            // available.
            node.saFwdRedeem = node.saRevRedeem;

            if (pathState.saInReq >= zero)
            {
                // Limit by send max.
                node.saFwdRedeem = std::min (
                    node.saFwdRedeem, pathState.saInReq - pathState.saInAct);
            }

            pathState.saInPass    = node.saFwdRedeem;

            node.saFwdIssue = node.saFwdRedeem == node.saRevRedeem
                // Fully redeemed.
                ? node.saRevIssue : STAmount (node.saRevIssue);

            if (!!node.saFwdIssue && pathState.saInReq >= zero)
            {
                // Limit by send max.
                node.saFwdIssue = std::min (
                    node.saFwdIssue,
                    pathState.saInReq - pathState.saInAct - node.saFwdRedeem);
            }

            pathState.saInPass += node.saFwdIssue;

            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeAccountFwd: ^ --> ACCOUNT --> account :"
                << " saInReq=" << pathState.saInReq
                << " saInAct=" << pathState.saInAct
                << " node.saFwdRedeem:" << node.saFwdRedeem
                << " node.saRevIssue:" << node.saRevIssue
                << " node.saFwdIssue:" << node.saFwdIssue
                << " pathState.saInPass:" << pathState.saInPass;
        }
        else if (nodeIndex == lastNodeIndex)
        {
            // account --> ACCOUNT --> $
            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeAccountFwd: account --> ACCOUNT --> $ :"
                << " previousAccountID="
                << RippleAddress::createHumanAccountID (previousAccountID)
                << " node.uAccountID="
                << RippleAddress::createHumanAccountID (node.uAccountID)
                << " previousNode.saFwdRedeem:" << previousNode.saFwdRedeem
                << " previousNode.saFwdIssue:" << previousNode.saFwdIssue;

            // Last node. Accept all funds. Calculate amount actually to credit.

            STAmount& saCurReceive = pathState.saOutPass;

            STAmount saIssueCrd = uQualityIn >= QUALITY_ONE
                    ? previousNode.saFwdIssue  // No fee.
                    : STAmount::mulRound (
                          previousNode.saFwdIssue,
                          STAmount (CURRENCY_ONE, ACCOUNT_ONE, uQualityIn, -9),
                          true); // Amount to credit.

            // Amount to credit. Credit for less than received as a surcharge.
            saCurReceive    = previousNode.saFwdRedeem + saIssueCrd;

            if (saCurReceive)
            {
                // Actually receive.
                errorCode = rippleCalc.mActiveLedger.rippleCredit (
                    previousAccountID, node.uAccountID,
                    previousNode.saFwdRedeem + previousNode.saFwdIssue, false);
            }
            else
            {
                // After applying quality, total payment was microscopic.
                errorCode   = tecPATH_DRY;
            }
        }
        else
        {
            // account --> ACCOUNT --> account
            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeAccountFwd: account --> ACCOUNT --> account";

            node.saFwdRedeem.clear (node.saRevRedeem);
            node.saFwdIssue.clear (node.saRevIssue);

            // Previous redeem part 1: redeem -> redeem
            if (previousNode.saFwdRedeem && node.saRevRedeem)
                // Previous wants to redeem.
            {
                // Rate : 1.0 : quality out
                calcNodeRipple (
                    rippleCalc,
                    QUALITY_ONE, uQualityOut, previousNode.saFwdRedeem, node.saRevRedeem,
                    saPrvRedeemAct, node.saFwdRedeem, uRateMax);
            }

            // Previous issue part 1: issue -> redeem
            if (previousNode.saFwdIssue != saPrvIssueAct
                // Previous wants to issue.
                && node.saRevRedeem != node.saFwdRedeem)
                // Current has more to redeem to next.
            {
                // Rate: quality in : quality out
                calcNodeRipple (
                    rippleCalc,
                    uQualityIn, uQualityOut, previousNode.saFwdIssue, node.saRevRedeem,
                    saPrvIssueAct, node.saFwdRedeem, uRateMax);
            }

            // Previous redeem part 2: redeem -> issue.
            if (previousNode.saFwdRedeem != saPrvRedeemAct
                // Previous still wants to redeem.
                && node.saRevRedeem == node.saFwdRedeem
                // Current redeeming is done can issue.
                && node.saRevIssue)
                // Current wants to issue.
            {
                // Rate : 1.0 : transfer_rate
                calcNodeRipple (
                    rippleCalc, QUALITY_ONE,
                    rippleCalc.mActiveLedger.rippleTransferRate (node.uAccountID),
                    previousNode.saFwdRedeem, node.saRevIssue, saPrvRedeemAct,
                    node.saFwdIssue, uRateMax);
            }

            // Previous issue part 2 : issue -> issue
            if (previousNode.saFwdIssue != saPrvIssueAct
                // Previous wants to issue.
                && node.saRevRedeem == node.saFwdRedeem
                // Current redeeming is done can issue.
                && node.saRevIssue)
                // Current wants to issue.
            {
                // Rate: quality in : 1.0
                calcNodeRipple (
                    rippleCalc,
                    uQualityIn, QUALITY_ONE, previousNode.saFwdIssue, node.saRevIssue,
                    saPrvIssueAct, node.saFwdIssue, uRateMax);
            }

            STAmount saProvide = node.saFwdRedeem + node.saFwdIssue;

            // Adjust prv --> cur balance : take all inbound
            errorCode   = saProvide
                ? rippleCalc.mActiveLedger.rippleCredit (
                    previousAccountID, node.uAccountID,
                    previousNode.saFwdRedeem + previousNode.saFwdIssue, false)
                : tecPATH_DRY;
        }
    }
    else if (previousNodeIsAccount && !nextNodeIsAccount)
    {
        // Current account is issuer to next offer.
        // Determine deliver to offer amount.
        // Don't adjust outbound balances- keep funds with issuer as limbo.
        // If issuer hold's an offer owners inbound IOUs, there is no fee and
        // redeem/issue will transparently happen.

        if (nodeIndex)
        {
            // Non-XRP, current node is the issuer.
            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeAccountFwd: account --> ACCOUNT --> offer";

            node.saFwdDeliver.clear (node.saRevDeliver);

            // redeem -> issue/deliver.
            // Previous wants to redeem.
            // Current is issuing to an offer so leave funds in account as
            // "limbo".
            if (previousNode.saFwdRedeem)
                // Previous wants to redeem.
            {
                // Rate : 1.0 : transfer_rate
                // XXX Is having the transfer rate here correct?
                calcNodeRipple (
                    rippleCalc, QUALITY_ONE,
                    rippleCalc.mActiveLedger.rippleTransferRate (node.uAccountID),
                    previousNode.saFwdRedeem, node.saRevDeliver, saPrvRedeemAct,
                    node.saFwdDeliver, uRateMax);
            }

            // issue -> issue/deliver
            if (previousNode.saFwdRedeem == saPrvRedeemAct
                // Previous done redeeming: Previous has no IOUs.
                && previousNode.saFwdIssue)
                // Previous wants to issue. To next must be ok.
            {
                // Rate: quality in : 1.0
                calcNodeRipple (
                    rippleCalc,
                    uQualityIn, QUALITY_ONE, previousNode.saFwdIssue, node.saRevDeliver,
                    saPrvIssueAct, node.saFwdDeliver, uRateMax);
            }

            // Adjust prv --> cur balance : take all inbound
            errorCode   = node.saFwdDeliver
                ? rippleCalc.mActiveLedger.rippleCredit (
                    previousAccountID, node.uAccountID,
                    previousNode.saFwdRedeem + previousNode.saFwdIssue, false)
                : tecPATH_DRY;  // Didn't actually deliver anything.
        }
        else
        {
            // Delivering amount requested from downstream.
            node.saFwdDeliver = node.saRevDeliver;

            // If limited, then limit by send max and available.
            if (pathState.saInReq >= zero)
            {
                // Limit by send max.
                node.saFwdDeliver = std::min (node.saFwdDeliver,
                                            pathState.saInReq - pathState.saInAct);

                // Limit XRP by available. No limit for non-XRP as issuer.
                if (node.uCurrencyID.isZero ())
                    node.saFwdDeliver = std::min (
                        node.saFwdDeliver,
                        rippleCalc.mActiveLedger.accountHolds (
                            node.uAccountID, CURRENCY_XRP, ACCOUNT_XRP));

            }

            // Record amount sent for pass.
            pathState.saInPass    = node.saFwdDeliver;

            if (!node.saFwdDeliver)
            {
                errorCode   = tecPATH_DRY;
            }
            else if (!!node.uCurrencyID)
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
                errorCode = rippleCalc.mActiveLedger.accountSend (
                    node.uAccountID, ACCOUNT_XRP, node.saFwdDeliver);
            }
        }
    }
    else if (!previousNodeIsAccount && nextNodeIsAccount)
    {
        if (nodeIndex == lastNodeIndex)
        {
            // offer --> ACCOUNT --> $
            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeAccountFwd: offer --> ACCOUNT --> $ : "
                << previousNode.saFwdDeliver;

            STAmount& saCurReceive = pathState.saOutPass;

            // Amount to credit.
            saCurReceive    = previousNode.saFwdDeliver;

            // No income balance adjustments necessary.  The paying side inside
            // the offer paid to this account.
        }
        else
        {
            // offer --> ACCOUNT --> account
            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeAccountFwd: offer --> ACCOUNT --> account";

            node.saFwdRedeem.clear (node.saRevRedeem);
            node.saFwdIssue.clear (node.saRevIssue);

            // deliver -> redeem
            if (previousNode.saFwdDeliver && node.saRevRedeem)
                // Previous wants to deliver and can current redeem.
            {
                // Rate : 1.0 : quality out
                calcNodeRipple (
                    rippleCalc,
                    QUALITY_ONE, uQualityOut, previousNode.saFwdDeliver, node.saRevRedeem,
                    saPrvDeliverAct, node.saFwdRedeem, uRateMax);
            }

            // deliver -> issue
            // Wants to redeem and current would and can issue.
            if (previousNode.saFwdDeliver != saPrvDeliverAct
                // Previous still wants to deliver.
                && node.saRevRedeem == node.saFwdRedeem
                // Current has more to redeem to next.
                && node.saRevIssue)
                // Current wants issue.
            {
                // Rate : 1.0 : transfer_rate
                calcNodeRipple (
                    rippleCalc, QUALITY_ONE,
                    rippleCalc.mActiveLedger.rippleTransferRate (node.uAccountID),
                    previousNode.saFwdDeliver, node.saRevIssue, saPrvDeliverAct,
                    node.saFwdIssue, uRateMax);
            }

            // No income balance adjustments necessary.  The paying side inside
            // the offer paid and the next link will receive.
            STAmount saProvide = node.saFwdRedeem + node.saFwdIssue;

            if (!saProvide)
                errorCode = tecPATH_DRY;
        }
    }
    else
    {
        // offer --> ACCOUNT --> offer
        // deliver/redeem -> deliver/issue.
        WriteLog (lsTRACE, RippleCalc)
            << "calcNodeAccountFwd: offer --> ACCOUNT --> offer";

        node.saFwdDeliver.clear (node.saRevDeliver);

        if (previousNode.saFwdDeliver
            // Previous wants to deliver
            && node.saRevIssue)
            // Current wants issue.
        {
            // Rate : 1.0 : transfer_rate
            calcNodeRipple (
                rippleCalc, QUALITY_ONE,
                rippleCalc.mActiveLedger.rippleTransferRate (node.uAccountID),
                previousNode.saFwdDeliver, node.saRevDeliver, saPrvDeliverAct,
                node.saFwdDeliver, uRateMax);
        }

        // No income balance adjustments necessary.  The paying side inside the
        // offer paid and the next link will receive.
        if (!node.saFwdDeliver)
            errorCode   = tecPATH_DRY;
    }

    return errorCode;
}

} // ripple
