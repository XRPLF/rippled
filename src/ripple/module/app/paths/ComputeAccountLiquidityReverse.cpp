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

// Calculate saPrvRedeemReq, saPrvIssueReq, saPrvDeliver from saCur, based on
// required deliverable, propagate redeem, issue (for accounts) and deliver
// requests (for order books) to the previous node.
//
// Inflate amount requested by required fees.
// Reedems are limited based on IOUs previous has on hand.
// Issues are limited based on credit limits and amount owed.
//
// Currency cannot be XRP because we are rippling.
//
// No permanent account balance adjustments as we don't know how much is going
// to actually be pushed through yet - changes are only in the scratch pad
// ledger.
//
// <-- tesSUCCESS or tecPATH_DRY

TER computeReverseLiquidityForAccount (
    RippleCalc& rippleCalc,
    const unsigned int nodeIndex, PathState& pathState,
    const bool bMultiQuality)
{
    TER terResult = tesSUCCESS;
    auto const lastNodeIndex = pathState.nodes().size () - 1;
    auto const isFinalNode = (nodeIndex == lastNodeIndex);

    // 0 quality means none has yet been determined.
    std::uint64_t uRateMax = 0;

    auto& previousNode = pathState.nodes()[nodeIndex ? nodeIndex - 1 : 0];
    auto& node = pathState.nodes()[nodeIndex];
    auto& nextNode = pathState.nodes()[isFinalNode ? lastNodeIndex : nodeIndex + 1];

    // Current is allowed to redeem to next.
    const bool previousNodeIsAccount = !nodeIndex || previousNode.isAccount();
    const bool nextNodeIsAccount = isFinalNode || nextNode.isAccount();

    const uint160& previousAccountID = previousNodeIsAccount
        ? previousNode.account_ : node.account_;
    const uint160& nextAccountID = nextNodeIsAccount ? nextNode.account_
        : node.account_;   // Offers are always issue.

    // This is the quality from from the previous node to this one.
    const std::uint32_t uQualityIn
         = (nodeIndex != 0)
            ? rippleCalc.mActiveLedger.rippleQualityIn (
                node.account_, previousAccountID, node.currency_)
            : QUALITY_ONE;

    // And this is the quality from the next one to this one.
    const std::uint32_t uQualityOut
        = (nodeIndex != lastNodeIndex)
            ? rippleCalc.mActiveLedger.rippleQualityOut (
                node.account_, nextAccountID, node.currency_)
            : QUALITY_ONE;

    // For previousNodeIsAccount:
    // Previous account is already owed.
    const STAmount saPrvOwed = (previousNodeIsAccount && nodeIndex != 0)
        ? rippleCalc.mActiveLedger.rippleOwed (
            node.account_, previousAccountID, node.currency_)
        : STAmount (node.currency_, node.account_);

    // The limit amount that the previous account may owe.
    const STAmount saPrvLimit = (previousNodeIsAccount && nodeIndex != 0)
        ? rippleCalc.mActiveLedger.rippleLimit (
            node.account_, previousAccountID, node.currency_)
        : STAmount (node.currency_, node.account_);

    // Next account is owed.
    const STAmount saNxtOwed = (nextNodeIsAccount && nodeIndex != lastNodeIndex)
        ? rippleCalc.mActiveLedger.rippleOwed (
            node.account_, nextAccountID, node.currency_)
        : STAmount (node.currency_, node.account_);

    WriteLog (lsTRACE, RippleCalc)
        << "computeReverseLiquidityForAccount>"
        << " nodeIndex=%d/%d" << nodeIndex << "/" << lastNodeIndex
        << " previousAccountID=" << previousAccountID
        << " node.account_=" << node.account_
        << " nextAccountID=" << nextAccountID
        << " currency_=" << node.currency_
        << " uQualityIn=" << uQualityIn
        << " uQualityOut=" << uQualityOut
        << " saPrvOwed=" << saPrvOwed
        << " saPrvLimit=" << saPrvLimit;

    // Requests are computed to be the maximum flow possible.
    // Previous can redeem the owed IOUs it holds.
    const STAmount saPrvRedeemReq  = (saPrvOwed > zero)
        ? saPrvOwed
        : STAmount (saPrvOwed.getCurrency (), saPrvOwed.getIssuer ());

    // This is the amount we're actually going to be setting for the previous
    // node.
    STAmount& saPrvRedeemAct = previousNode.saRevRedeem;

    // Previous can issue up to limit minus whatever portion of limit already
    // used (not including redeemable amount) - another "maximum flow".
    const STAmount saPrvIssueReq = (saPrvOwed < zero)
        ? saPrvLimit + saPrvOwed : saPrvLimit;
    STAmount& saPrvIssueAct = previousNode.saRevIssue;

    // Precompute these values in case we have an order book.
    auto deliverCurrency = previousNode.saRevDeliver.getCurrency ();
    const STAmount saPrvDeliverReq (
        deliverCurrency, previousNode.saRevDeliver.getIssuer (), -1);
    // Unlimited delivery.

    STAmount&       saPrvDeliverAct = previousNode.saRevDeliver;

    // For nextNodeIsAccount
    const STAmount& saCurRedeemReq  = node.saRevRedeem;

    // Set to zero, because we're trying to hit the previous node.
    STAmount saCurRedeemAct (
        saCurRedeemReq.getCurrency (), saCurRedeemReq.getIssuer ());

    const STAmount& saCurIssueReq = node.saRevIssue;
    // Track the amount we actually redeem.
    STAmount saCurIssueAct (
        saCurIssueReq.getCurrency (), saCurIssueReq.getIssuer ());

    // For !nextNodeIsAccount
    const STAmount& saCurDeliverReq = node.saRevDeliver;
    STAmount saCurDeliverAct (
        saCurDeliverReq.getCurrency (), saCurDeliverReq.getIssuer ());

    WriteLog (lsTRACE, RippleCalc)
        << "computeReverseLiquidityForAccount:"
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

    if (nodeIndex == 0)
    {
        // ^ --> ACCOUNT -->  account|offer
        // Nothing to do, there is no previous to adjust.
        //
        // TODO(tom): we could have skipped all that setup and just left
        // or even just never call this whole routine on nodeIndex = 0!
    }

    // The next four cases correspond to the table at the bottom of this Wiki
    // page section: https://ripple.com/wiki/Transit_Fees#Implementation
    else if (previousNodeIsAccount && nextNodeIsAccount)
    {
        if (isFinalNode)
        {
            // account --> ACCOUNT --> $
            // Overall deliverable.
            const STAmount saCurWantedReq = std::min (
                pathState.outReq() - pathState.outAct(),
                saPrvLimit + saPrvOwed);
            STAmount saCurWantedAct (
                saCurWantedReq.getCurrency (), saCurWantedReq.getIssuer ());

            WriteLog (lsTRACE, RippleCalc)
                << "computeReverseLiquidityForAccount: account --> ACCOUNT --> $ :"
                << " saCurWantedReq=" << saCurWantedReq;

            // Calculate redeem
            if (saPrvRedeemReq) // Previous has IOUs to redeem.
            {
                // Redeem your own IOUs at 1:1

                saCurWantedAct = std::min (saPrvRedeemReq, saCurWantedReq);
                saPrvRedeemAct = saCurWantedAct;

                uRateMax = STAmount::uRateOne;

                WriteLog (lsTRACE, RippleCalc)
                    << "computeReverseLiquidityForAccount: Redeem at 1:1"
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
                && saPrvIssueReq)  // Will accept IOUs from previous.
            {
                // Rate: quality in : 1.0

                // If we previously redeemed and this has a poorer rate, this
                // won't be included the current increment.
                computeRippleLiquidity (
                    rippleCalc,
                    uQualityIn, QUALITY_ONE,
                    saPrvIssueReq, saCurWantedReq,
                    saPrvIssueAct, saCurWantedAct, uRateMax);

                WriteLog (lsTRACE, RippleCalc)
                    << "computeReverseLiquidityForAccount: Issuing: Rate: quality in : 1.0"
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
            // Not final node.
            // account --> ACCOUNT --> account
            saPrvRedeemAct.clear (saPrvRedeemReq);
            saPrvIssueAct.clear (saPrvIssueReq);

            // redeem (part 1) -> redeem
            if (saCurRedeemReq
                // Next wants IOUs redeemed from current account.
                && saPrvRedeemReq)
                // Previous has IOUs to redeem to the current account.
            {
                // TODO(tom): add English.
                // Rate : 1.0 : quality out - we must accept our own IOUs as 1:1.
                computeRippleLiquidity (
                    rippleCalc,
                    QUALITY_ONE, uQualityOut,
                    saPrvRedeemReq, saCurRedeemReq,
                    saPrvRedeemAct, saCurRedeemAct, uRateMax);

                WriteLog (lsTRACE, RippleCalc)
                    << "computeReverseLiquidityForAccount: "
                    << "Rate : 1.0 : quality out"
                    << " saPrvRedeemAct:" << saPrvRedeemAct
                    << " saCurRedeemAct:" << saCurRedeemAct;
            }

            // issue (part 1) -> redeem
            if (saCurRedeemReq != saCurRedeemAct
                // The current node has more IOUs to redeem.
                && saPrvRedeemAct == saPrvRedeemReq)
                // The previous node has no IOUs to redeem remaining, so issues.
            {
                // Rate: quality in : quality out
                computeRippleLiquidity (
                    rippleCalc,
                    uQualityIn, uQualityOut,
                    saPrvIssueReq, saCurRedeemReq,
                    saPrvIssueAct, saCurRedeemAct, uRateMax);

                WriteLog (lsTRACE, RippleCalc)
                    << "computeReverseLiquidityForAccount: "
                    << "Rate: quality in : quality out:"
                    << " saPrvIssueAct:" << saPrvIssueAct
                    << " saCurRedeemAct:" << saCurRedeemAct;
            }

            // redeem (part 2) -> issue.
            if (saCurIssueReq   // Next wants IOUs issued.
                // TODO(tom): this condition seems redundant.
                && saCurRedeemAct == saCurRedeemReq
                // Can only issue if completed redeeming.
                && saPrvRedeemAct != saPrvRedeemReq)
                // Did not complete redeeming previous IOUs.
            {
                // Rate : 1.0 : transfer_rate
                computeRippleLiquidity (
                    rippleCalc,
                    QUALITY_ONE,
                    rippleCalc.mActiveLedger.rippleTransferRate (node.account_),
                    saPrvRedeemReq, saCurIssueReq,
                    saPrvRedeemAct, saCurIssueAct, uRateMax);

                WriteLog (lsDEBUG, RippleCalc)
                    << "computeReverseLiquidityForAccount: "
                    << "Rate : 1.0 : transfer_rate:"
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
                computeRippleLiquidity (
                    rippleCalc,
                    uQualityIn, QUALITY_ONE,
                    saPrvIssueReq, saCurIssueReq,
                    saPrvIssueAct, saCurIssueAct, uRateMax);

                WriteLog (lsTRACE, RippleCalc)
                    << "computeReverseLiquidityForAccount: "
                    << "Rate: quality in : 1.0:"
                    << " saPrvIssueAct:" << saPrvIssueAct
                    << " saCurIssueAct:" << saCurIssueAct;
            }

            if (!saCurRedeemAct && !saCurIssueAct)
            {
                // Did not make progress.
                terResult = tecPATH_DRY;
            }

            WriteLog (lsTRACE, RippleCalc)
                << "computeReverseLiquidityForAccount: "
                << "^|account --> ACCOUNT --> account :"
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
            << "computeReverseLiquidityForAccount: "
            << "account --> ACCOUNT --> offer";

        saPrvRedeemAct.clear (saPrvRedeemReq);
        saPrvIssueAct.clear (saPrvIssueReq);

        // We have three cases: the nxt offer can be owned by current account,
        // previous account or some third party account.
        //
        // Also, the current account may or may not have a redeemable balance
        // with the account for the next offer, so we don't yet know if we're
        // redeeming or issuing.
        //
        // TODO(tom): Make sure deliver was cleared, or check actual is zero.
        // redeem -> deliver/issue.
        if (saPrvOwed > zero                    // Previous has IOUs to redeem.
            && saCurDeliverReq)                 // Need some issued.
        {
            // Rate : 1.0 : transfer_rate
            computeRippleLiquidity (
                rippleCalc, QUALITY_ONE,
                rippleCalc.mActiveLedger.rippleTransferRate (node.account_),
                saPrvRedeemReq, saCurDeliverReq,
                saPrvRedeemAct, saCurDeliverAct, uRateMax);
        }

        // issue -> deliver/issue
        if (saPrvRedeemReq == saPrvRedeemAct    // Previously redeemed all owed.
            && saCurDeliverReq != saCurDeliverAct)  // Still need some issued.
        {
            // Rate: quality in : 1.0
            computeRippleLiquidity (
                rippleCalc,
                uQualityIn, QUALITY_ONE,
                saPrvIssueReq, saCurDeliverReq,
                saPrvIssueAct, saCurDeliverAct, uRateMax);
        }

        if (!saCurDeliverAct)
        {
            // Must want something.
            terResult   = tecPATH_DRY;
        }

        WriteLog (lsTRACE, RippleCalc)
            << "computeReverseLiquidityForAccount: "
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
            //
            // This is the final node; we can't look to the right to get values;
            // we have to go up to get the out value for the entire path state.
            const STAmount& saCurWantedReq  =
                    pathState.outReq() - pathState.outAct();
            STAmount saCurWantedAct = saCurWantedReq.zeroed();

            WriteLog (lsTRACE, RippleCalc)
                << "computeReverseLiquidityForAccount: "
                << "offer --> ACCOUNT --> $ :"
                << " saCurWantedReq:" << saCurWantedReq
                << " saOutAct:" << pathState.outAct()
                << " saOutReq:" << pathState.outReq();

            if (saCurWantedReq <= zero)
            {
                // TEMPORARY emergency fix
                //
                // TODO(tom): why can't saCurWantedReq be -1 if you want to
                // compute maximum liquidity?  This might be unimplemented
                // functionality.  TODO(tom): should the same check appear in
                // other paths or even be pulled up?
                WriteLog (lsFATAL, RippleCalc) << "CurWantReq was not positive";
                return tefEXCEPTION;
            }

            assert (saCurWantedReq > zero); // FIXME: We got one of these
            // The previous node is an offer;  we are receiving our own currency;

            // The previous order book's entries might hold our issuances; might
            // not hold our issuances; might be our own offer.
            //
            // Assume the worst case, the case which costs the most to go
            // through, which is that it is not our own offer or our own
            // issuances.  Later on the forward pass we may be able to do
            // better.
            //
            // TODO: this comment applies generally to this section - move it up
            // to a document.

            // Rate: quality in : 1.0
            computeRippleLiquidity (
                rippleCalc,
                uQualityIn, QUALITY_ONE,
                saPrvDeliverReq, saCurWantedReq,
                saPrvDeliverAct, saCurWantedAct, uRateMax);

            if (!saCurWantedAct)
            {
                // Must have processed something.
                terResult   = tecPATH_DRY;
            }

            WriteLog (lsTRACE, RippleCalc)
                << "computeReverseLiquidityForAccount:"
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
                << "computeReverseLiquidityForAccount: "
                << "offer --> ACCOUNT --> account :"
                << " saCurRedeemReq:" << saCurRedeemReq
                << " saCurIssueReq:" << saCurIssueReq;

            // deliver -> redeem
            // TODO(tom): now we have more checking in nodeRipple, these checks
            // might be redundant.
            if (saCurRedeemReq)  // Next wants us to redeem.
            {
                // cur holds IOUs from the account to the right, the nxt
                // account.  If someone is making the current account get rid of
                // the nxt account's IOUs, then charge the input for quality out.
                //
                // Rate : 1.0 : quality out
                computeRippleLiquidity (
                    rippleCalc,
                    QUALITY_ONE, uQualityOut,
                    saPrvDeliverReq, saCurRedeemReq,
                    saPrvDeliverAct, saCurRedeemAct, uRateMax);
            }

            // deliver -> issue.
            if (saCurRedeemReq == saCurRedeemAct
                // Can only issue if previously redeemed all.
                && saCurIssueReq)
                // Need some issued.
            {
                // Rate : 1.0 : transfer_rate
                computeRippleLiquidity (
                    rippleCalc,
                    QUALITY_ONE,
                    rippleCalc.mActiveLedger.rippleTransferRate (node.account_),
                    saPrvDeliverReq, saCurIssueReq, saPrvDeliverAct,
                    saCurIssueAct, uRateMax);
            }

            WriteLog (lsTRACE, RippleCalc)
                << "computeReverseLiquidityForAccount:"
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
            << "computeReverseLiquidityForAccount: offer --> ACCOUNT --> offer";

        // Rate : 1.0 : transfer_rate
        computeRippleLiquidity (
            rippleCalc,
            QUALITY_ONE,
            rippleCalc.mActiveLedger.rippleTransferRate (node.account_),
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

} // path
} // ripple
