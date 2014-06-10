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

// Called to drive the from the first offer node in a chain.
//
// - Offer input is in issuer/limbo.
// - Current offers consumed.
//   - Current offer owners debited.
//   - Transfer fees credited to issuer.
//   - Payout to issuer or limbo.
// - Deliver is set without transfer fees.
TER computeForwardLiquidityForOffer (
    RippleCalc& rippleCalc,
    const unsigned int nodeIndex,
    PathState& pathState,
    const bool bMultiQuality)
{
    auto& previousNode = pathState.nodes() [nodeIndex - 1];

    if (previousNode.account_ == zero)
        return tesSUCCESS;

    // Previous is an account node, resolve its deliver.
    STAmount saInAct;
    STAmount saInFees;

    auto resultCode = nodeDeliverFwd (
        rippleCalc,
        nodeIndex,
        pathState,
        bMultiQuality,
        previousNode.account_,
        previousNode.saFwdDeliver, // Previous is sending this much.
        saInAct,
        saInFees);

    assert (resultCode != tesSUCCESS ||
            previousNode.saFwdDeliver == saInAct + saInFees);
    return resultCode;

}

// Called to drive from the last offer node in a chain.
// nodeIndex doesn't refer to the node at either end because both ends must be
// an account.
TER computeReverseLiquidityForOffer (
    RippleCalc& rippleCalc,
    const unsigned int nodeIndex,
    PathState& pathState,
    const bool bMultiQuality)
{
    auto& nextNode = pathState.nodes() [nodeIndex + 1];
    if (nextNode.account_ == zero)
    {
        WriteLog (lsTRACE, RippleCalc)
            << "computeReverseLiquidityForOffer: "
            << "OFFER --> offer: nodeIndex=" << nodeIndex;
        return tesSUCCESS;

        // This control structure ensures nodeDeliverRev is only called for the
        // rightmost offer in a chain of offers - which means that
        // nodeDeliverRev has to take all of those offers into consideration.
    }

    auto& node = pathState.nodes() [nodeIndex];

    // Next is an account node, resolve current offer node's deliver.
    STAmount saDeliverAct;

    WriteLog (lsTRACE, RippleCalc)
        << "computeReverseLiquidityForOffer: OFFER --> account:"
        << " nodeIndex=" << nodeIndex
        << " saRevDeliver=" << node.saRevDeliver;

    return nodeDeliverRev (
        rippleCalc,
        nodeIndex,
        pathState,
        bMultiQuality,
        nextNode.account_,

        // The next node wants the current node to deliver this much:
        node.saRevDeliver,
        saDeliverAct);
}

}  // path
}  // ripple
