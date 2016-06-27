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
#include <ripple/app/paths/Credit.h>
#include <ripple/app/paths/cursor/RippleLiquidity.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/Quality.h>

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

TER PathCursor::reverseLiquidityForAccount () const
{
    TER terResult = tesSUCCESS;
    auto const lastNodeIndex = nodeSize () - 1;
    auto const isFinalNode = (nodeIndex_ == lastNodeIndex);

    // 0 quality means none has yet been determined.
    std::uint64_t uRateMax = 0;

    // Current is allowed to redeem to next.
    const bool previousNodeIsAccount = !nodeIndex_ ||
            previousNode().isAccount();

    const bool nextNodeIsAccount = isFinalNode || nextNode().isAccount();

    AccountID const& previousAccountID = previousNodeIsAccount
        ? previousNode().account_ : node().account_;
    AccountID const& nextAccountID = nextNodeIsAccount ? nextNode().account_
        : node().account_;   // Offers are always issue.

    // This is the quality from from the previous node to this one.
    auto const qualityIn
         = (nodeIndex_ != 0)
            ? quality_in (view(),
                node().account_,
                previousAccountID,
                node().issue_.currency)
            : parityRate;

    // And this is the quality from the next one to this one.
    auto const qualityOut
        = (nodeIndex_ != lastNodeIndex)
            ? quality_out (view(),
                node().account_,
                nextAccountID,
                node().issue_.currency)
            : parityRate;

    // For previousNodeIsAccount:
    // Previous account is already owed.
    const STAmount saPrvOwed = (previousNodeIsAccount && nodeIndex_ != 0)
        ? creditBalance (view(),
            node().account_,
            previousAccountID,
            node().issue_.currency)
        : STAmount (node().issue_);

    // The limit amount that the previous account may owe.
    const STAmount saPrvLimit = (previousNodeIsAccount && nodeIndex_ != 0)
        ? creditLimit (view(),
            node().account_,
            previousAccountID,
            node().issue_.currency)
        : STAmount (node().issue_);

    // Next account is owed.
    const STAmount saNxtOwed = (nextNodeIsAccount && nodeIndex_ != lastNodeIndex)
        ? creditBalance (view(),
            node().account_,
            nextAccountID,
            node().issue_.currency)
        : STAmount (node().issue_);

    JLOG (j_.trace())
        << "reverseLiquidityForAccount>"
        << " nodeIndex_=" << nodeIndex_ << "/" << lastNodeIndex
        << " previousAccountID=" << previousAccountID
        << " node.account_=" << node().account_
        << " nextAccountID=" << nextAccountID
        << " currency=" << node().issue_.currency
        << " qualityIn=" << qualityIn
        << " qualityOut=" << qualityOut
        << " saPrvOwed=" << saPrvOwed
        << " saPrvLimit=" << saPrvLimit;

    // Requests are computed to be the maximum flow possible.
    // Previous can redeem the owed IOUs it holds.
    const STAmount saPrvRedeemReq  = (saPrvOwed > zero)
        ? saPrvOwed
        : STAmount (saPrvOwed.issue ());

    // Previous can issue up to limit minus whatever portion of limit already
    // used (not including redeemable amount) - another "maximum flow".
    const STAmount saPrvIssueReq = (saPrvOwed < zero)
        ? saPrvLimit + saPrvOwed : saPrvLimit;

    // Precompute these values in case we have an order book.
    auto deliverCurrency = previousNode().saRevDeliver.getCurrency ();
    const STAmount saPrvDeliverReq (
        {deliverCurrency, previousNode().saRevDeliver.getIssuer ()}, -1);
    // -1 means unlimited delivery.

    // Set to zero, because we're trying to hit the previous node.
    auto saCurRedeemAct = node().saRevRedeem.zeroed();

    // Track the amount we actually redeem.
    auto saCurIssueAct = node().saRevIssue.zeroed();

    // For !nextNodeIsAccount
    auto saCurDeliverAct  = node().saRevDeliver.zeroed();

    JLOG (j_.trace())
        << "reverseLiquidityForAccount:"
        << " saPrvRedeemReq:" << saPrvRedeemReq
        << " saPrvIssueReq:" << saPrvIssueReq
        << " previousNode.saRevDeliver:" << previousNode().saRevDeliver
        << " saPrvDeliverReq:" << saPrvDeliverReq
        << " node.saRevRedeem:" << node().saRevRedeem
        << " node.saRevIssue:" << node().saRevIssue
        << " saNxtOwed:" << saNxtOwed;

    // VFALCO-FIXME this generates errors
    //JLOG (j_.trace()) << pathState_.getJson ();

    // Current redeem req can't be more than IOUs on hand.
    assert (!node().saRevRedeem || -saNxtOwed >= node().saRevRedeem);
    assert (!node().saRevIssue  // If not issuing, fine.
            || saNxtOwed >= zero
            // saNxtOwed >= 0: Sender not holding next IOUs, saNxtOwed < 0:
            // Sender holding next IOUs.
            || -saNxtOwed == node().saRevRedeem);
    // If issue req, then redeem req must consume all owed.

    if (nodeIndex_ == 0)
    {
        // ^ --> ACCOUNT -->  account|offer
        // Nothing to do, there is no previous to adjust.
        //
        // TODO(tom): we could have skipped all that setup and just left
        // or even just never call this whole routine on nodeIndex_ = 0!
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
                pathState_.outReq() - pathState_.outAct(),
                saPrvLimit + saPrvOwed);
            auto saCurWantedAct = saCurWantedReq.zeroed ();

            JLOG (j_.trace())
                << "reverseLiquidityForAccount: account --> "
                << "ACCOUNT --> $ :"
                << " saCurWantedReq=" << saCurWantedReq;

            // Calculate redeem
            if (saPrvRedeemReq) // Previous has IOUs to redeem.
            {
                // Redeem your own IOUs at 1:1

                saCurWantedAct = std::min (saPrvRedeemReq, saCurWantedReq);
                previousNode().saRevRedeem = saCurWantedAct;

                uRateMax = STAmount::uRateOne;

                JLOG (j_.trace())
                    << "reverseLiquidityForAccount: Redeem at 1:1"
                    << " saPrvRedeemReq=" << saPrvRedeemReq
                    << " (available) previousNode.saRevRedeem="
                    << previousNode().saRevRedeem
                    << " uRateMax="
                    << amountFromQuality (uRateMax).getText ();
            }
            else
            {
                previousNode().saRevRedeem.clear (saPrvRedeemReq);
            }

            // Calculate issuing.
            previousNode().saRevIssue.clear (saPrvIssueReq);

            if (saCurWantedReq != saCurWantedAct // Need more.
                && saPrvIssueReq)  // Will accept IOUs from previous.
            {
                // Rate: quality in : 1.0

                // If we previously redeemed and this has a poorer rate, this
                // won't be included the current increment.
                rippleLiquidity (
                    rippleCalc_,
                    qualityIn,
                    parityRate,
                    saPrvIssueReq,
                    saCurWantedReq,
                    previousNode().saRevIssue,
                    saCurWantedAct,
                    uRateMax);

                JLOG (j_.trace())
                    << "reverseLiquidityForAccount: Issuing: Rate: "
                    << "quality in : 1.0"
                    << " previousNode.saRevIssue:" << previousNode().saRevIssue
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
            previousNode().saRevRedeem.clear (saPrvRedeemReq);
            previousNode().saRevIssue.clear (saPrvIssueReq);

            // redeem (part 1) -> redeem
            if (node().saRevRedeem
                // Next wants IOUs redeemed from current account.
                && saPrvRedeemReq)
                // Previous has IOUs to redeem to the current account.
            {
                // TODO(tom): add English.
                // Rate : 1.0 : quality out - we must accept our own IOUs
                // as 1:1.
                rippleLiquidity (
                    rippleCalc_,
                    parityRate,
                    qualityOut,
                    saPrvRedeemReq,
                    node().saRevRedeem,
                    previousNode().saRevRedeem,
                    saCurRedeemAct,
                    uRateMax);

                JLOG (j_.trace())
                    << "reverseLiquidityForAccount: "
                    << "Rate : 1.0 : quality out"
                    << " previousNode.saRevRedeem:" << previousNode().saRevRedeem
                    << " saCurRedeemAct:" << saCurRedeemAct;
            }

            // issue (part 1) -> redeem
            if (node().saRevRedeem != saCurRedeemAct
                // The current node has more IOUs to redeem.
                && previousNode().saRevRedeem == saPrvRedeemReq)
                // The previous node has no IOUs to redeem remaining, so issues.
            {
                // Rate: quality in : quality out
                rippleLiquidity (
                    rippleCalc_,
                    qualityIn,
                    qualityOut,
                    saPrvIssueReq,
                    node().saRevRedeem,
                    previousNode().saRevIssue,
                    saCurRedeemAct,
                    uRateMax);

                JLOG (j_.trace())
                    << "reverseLiquidityForAccount: "
                    << "Rate: quality in : quality out:"
                    << " previousNode.saRevIssue:" << previousNode().saRevIssue
                    << " saCurRedeemAct:" << saCurRedeemAct;
            }

            // redeem (part 2) -> issue.
            if (node().saRevIssue   // Next wants IOUs issued.
                // TODO(tom): this condition seems redundant.
                && saCurRedeemAct == node().saRevRedeem
                // Can only issue if completed redeeming.
                && previousNode().saRevRedeem != saPrvRedeemReq)
                // Did not complete redeeming previous IOUs.
            {
                // Rate : 1.0 : transfer_rate
                rippleLiquidity (
                    rippleCalc_,
                    parityRate,
                    transferRate (view(), node().account_),
                    saPrvRedeemReq,
                    node().saRevIssue,
                    previousNode().saRevRedeem,
                    saCurIssueAct,
                    uRateMax);

                JLOG (j_.debug())
                    << "reverseLiquidityForAccount: "
                    << "Rate : 1.0 : transfer_rate:"
                    << " previousNode.saRevRedeem:" << previousNode().saRevRedeem
                    << " saCurIssueAct:" << saCurIssueAct;
            }

            // issue (part 2) -> issue
            if (node().saRevIssue != saCurIssueAct
                // Need wants more IOUs issued.
                && saCurRedeemAct == node().saRevRedeem
                // Can only issue if completed redeeming.
                && saPrvRedeemReq == previousNode().saRevRedeem
                // Previously redeemed all owed IOUs.
                && saPrvIssueReq)
                // Previous can issue.
            {
                // Rate: quality in : 1.0
                rippleLiquidity (
                    rippleCalc_,
                    qualityIn,
                    parityRate,
                    saPrvIssueReq,
                    node().saRevIssue,
                    previousNode().saRevIssue,
                    saCurIssueAct,
                    uRateMax);

                JLOG (j_.trace())
                    << "reverseLiquidityForAccount: "
                    << "Rate: quality in : 1.0:"
                    << " previousNode.saRevIssue:" << previousNode().saRevIssue
                    << " saCurIssueAct:" << saCurIssueAct;
            }

            if (!saCurRedeemAct && !saCurIssueAct)
            {
                // Did not make progress.
                terResult = tecPATH_DRY;
            }

            JLOG (j_.trace())
                << "reverseLiquidityForAccount: "
                << "^|account --> ACCOUNT --> account :"
                << " node.saRevRedeem:" << node().saRevRedeem
                << " node.saRevIssue:" << node().saRevIssue
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
        JLOG (j_.trace())
            << "reverseLiquidityForAccount: "
            << "account --> ACCOUNT --> offer";

        previousNode().saRevRedeem.clear (saPrvRedeemReq);
        previousNode().saRevIssue.clear (saPrvIssueReq);

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
            && node().saRevDeliver)                 // Need some issued.
        {
            // Rate : 1.0 : transfer_rate
            rippleLiquidity (
                rippleCalc_,
                parityRate,
                transferRate (view(), node().account_),
                saPrvRedeemReq,
                node().saRevDeliver,
                previousNode().saRevRedeem,
                saCurDeliverAct,
                uRateMax);
        }

        // issue -> deliver/issue
        if (saPrvRedeemReq == previousNode().saRevRedeem
            // Previously redeemed all owed.
            && node().saRevDeliver != saCurDeliverAct)  // Still need some issued.
        {
            // Rate: quality in : 1.0
            rippleLiquidity (
                rippleCalc_,
                qualityIn,
                parityRate,
                saPrvIssueReq,
                node().saRevDeliver,
                previousNode().saRevIssue,
                saCurDeliverAct,
                uRateMax);
        }

        if (!saCurDeliverAct)
        {
            // Must want something.
            terResult   = tecPATH_DRY;
        }

        JLOG (j_.trace())
            << "reverseLiquidityForAccount: "
            << " node.saRevDeliver:" << node().saRevDeliver
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
            STAmount const& saCurWantedReq  =
                    pathState_.outReq() - pathState_.outAct();
            STAmount saCurWantedAct = saCurWantedReq.zeroed();

            JLOG (j_.trace())
                << "reverseLiquidityForAccount: "
                << "offer --> ACCOUNT --> $ :"
                << " saCurWantedReq:" << saCurWantedReq
                << " saOutAct:" << pathState_.outAct()
                << " saOutReq:" << pathState_.outReq();

            if (saCurWantedReq <= zero)
            {
                assert(false);
                JLOG (j_.fatal()) << "CurWantReq was not positive";
                return tefEXCEPTION;
            }

            // The previous node is an offer; we are receiving our own currency.

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
            rippleLiquidity (
                rippleCalc_,
                qualityIn,
                parityRate,
                saPrvDeliverReq,
                saCurWantedReq,
                previousNode().saRevDeliver,
                saCurWantedAct,
                uRateMax);

            if (!saCurWantedAct)
            {
                // Must have processed something.
                terResult   = tecPATH_DRY;
            }

            JLOG (j_.trace())
                << "reverseLiquidityForAccount:"
                << " previousNode().saRevDeliver:" << previousNode().saRevDeliver
                << " saPrvDeliverReq:" << saPrvDeliverReq
                << " saCurWantedAct:" << saCurWantedAct
                << " saCurWantedReq:" << saCurWantedReq;
        }
        else
        {
            // offer --> ACCOUNT --> account
            // Note: offer is always delivering(redeeming) as account is issuer.
            JLOG (j_.trace())
                << "reverseLiquidityForAccount: "
                << "offer --> ACCOUNT --> account :"
                << " node.saRevRedeem:" << node().saRevRedeem
                << " node.saRevIssue:" << node().saRevIssue;

            // deliver -> redeem
            // TODO(tom): now we have more checking in nodeRipple, these checks
            // might be redundant.
            if (node().saRevRedeem)  // Next wants us to redeem.
            {
                // cur holds IOUs from the account to the right, the nxt
                // account.  If someone is making the current account get rid of
                // the nxt account's IOUs, then charge the input for quality
                // out.
                //
                // Rate : 1.0 : quality out
                rippleLiquidity (
                    rippleCalc_,
                    parityRate,
                    qualityOut,
                    saPrvDeliverReq,
                    node().saRevRedeem,
                    previousNode().saRevDeliver,
                    saCurRedeemAct,
                    uRateMax);
            }

            // deliver -> issue.
            if (node().saRevRedeem == saCurRedeemAct
                // Can only issue if previously redeemed all.
                && node().saRevIssue)
                // Need some issued.
            {
                // Rate : 1.0 : transfer_rate
                rippleLiquidity (
                    rippleCalc_,
                    parityRate,
                    transferRate (view(), node().account_),
                    saPrvDeliverReq,
                    node().saRevIssue,
                    previousNode().saRevDeliver,
                    saCurIssueAct,
                    uRateMax);
            }

            JLOG (j_.trace())
                << "reverseLiquidityForAccount:"
                << " saCurRedeemAct:" << saCurRedeemAct
                << " node.saRevRedeem:" << node().saRevRedeem
                << " previousNode.saRevDeliver:" << previousNode().saRevDeliver
                << " node.saRevIssue:" << node().saRevIssue;

            if (!previousNode().saRevDeliver)
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
        JLOG (j_.trace())
            << "reverseLiquidityForAccount: offer --> ACCOUNT --> offer";

        // Rate : 1.0 : transfer_rate
        rippleLiquidity (
            rippleCalc_,
            parityRate,
            transferRate (view(), node().account_),
            saPrvDeliverReq,
            node().saRevDeliver,
            previousNode().saRevDeliver,
            saCurDeliverAct,
            uRateMax);

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
