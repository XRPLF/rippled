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

// Called to drive the from the first offer node in a chain.
//
// - Offer input is in issuer/limbo.
// - Current offers consumed.
//   - Current offer owners debited.
//   - Transfer fees credited to issuer.
//   - Payout to issuer or limbo.
// - Deliver is set without transfer fees.
TER calcNodeOfferFwd (
    RippleCalc& rippleCalc,
    const unsigned int          nodeIndex,
    PathState&                  pathState,
    const bool                  bMultiQuality
)
{
    TER             errorCode;
    PathState::Node& previousNode = pathState.vpnNodes [nodeIndex - 1];

    if (!!previousNode.uAccountID)
    {
        // Previous is an account node, resolve its deliver.
        STAmount        saInAct;
        STAmount        saInFees;

        errorCode   = calcNodeDeliverFwd (
            rippleCalc,
            nodeIndex,
            pathState,
            bMultiQuality,
            previousNode.uAccountID,
            previousNode.saFwdDeliver, // Previous is sending this much.
            saInAct,
            saInFees);

        assert (errorCode != tesSUCCESS ||
                previousNode.saFwdDeliver == saInAct + saInFees);
    }
    else
    {
        // Previous is an offer. Deliver has already been resolved.
        errorCode   = tesSUCCESS;
    }

    return errorCode;

}

// Called to drive from the last offer node in a chain.
TER calcNodeOfferRev (
    RippleCalc& rippleCalc,
    const unsigned int          nodeIndex,
    PathState&                  pathState,
    const bool                  bMultiQuality)
{
    TER             errorCode;

    PathState::Node& node = pathState.vpnNodes [nodeIndex];
    PathState::Node& nextNode = pathState.vpnNodes [nodeIndex + 1];

    if (!!nextNode.uAccountID)
    {
        // Next is an account node, resolve current offer node's deliver.
        STAmount        saDeliverAct;

        WriteLog (lsTRACE, RippleCalc)
            << "calcNodeOfferRev: OFFER --> account:"
            << " nodeIndex=" << nodeIndex
            << " saRevDeliver=" << node.saRevDeliver;

        errorCode   = calcNodeDeliverRev (
            rippleCalc,
            nodeIndex,
            pathState,
            bMultiQuality,
            nextNode.uAccountID,

            // The next node wants the current node to deliver this much:
            node.saRevDeliver,
            saDeliverAct);
    }
    else
    {
        WriteLog (lsTRACE, RippleCalc)
            << "calcNodeOfferRev: OFFER --> offer: nodeIndex=" << nodeIndex;

        // Next is an offer. Deliver has already been resolved.
        errorCode   = tesSUCCESS;
    }

    return errorCode;
}

} // ripple
