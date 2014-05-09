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

// Called to drive the from the first offer node in a chain.
//
// - Offer input is in issuer/limbo.
// - Current offers consumed.
//   - Current offer owners debited.
//   - Transfer fees credited to issuer.
//   - Payout to issuer or limbo.
// - Deliver is set without transfer fees.
TER RippleCalc::calcNodeOfferFwd (
    const unsigned int          uNode,              // 0 < uNode < uLast
    PathState&                  psCur,
    const bool                  bMultiQuality
)
{
    TER             terResult;
    PathState::Node& pnPrv = psCur.vpnNodes [uNode - 1];

    if (!!pnPrv.uAccountID)
    {
        // Previous is an account node, resolve its deliver.
        STAmount        saInAct;
        STAmount        saInFees;

        terResult   = calcNodeDeliverFwd (
                          uNode,
                          psCur,
                          bMultiQuality,
                          pnPrv.uAccountID,
                          pnPrv.saFwdDeliver, // Previous is sending this much.
                          saInAct,
                          saInFees);

        assert (tesSUCCESS != terResult ||
                pnPrv.saFwdDeliver == saInAct + saInFees);
    }
    else
    {
        // Previous is an offer. Deliver has already been resolved.
        terResult   = tesSUCCESS;
    }

    return terResult;

}

// Called to drive from the last offer node in a chain.
TER RippleCalc::calcNodeOfferRev (
    const unsigned int          uNode,              // 0 < uNode < uLast
    PathState&                  psCur,
    const bool                  bMultiQuality)
{
    TER             terResult;

    PathState::Node& pnCur = psCur.vpnNodes [uNode];
    PathState::Node& pnNxt = psCur.vpnNodes [uNode + 1];

    if (!!pnNxt.uAccountID)
    {
        // Next is an account node, resolve current offer node's deliver.
        STAmount        saDeliverAct;

        WriteLog (lsTRACE, RippleCalc)
            << "calcNodeOfferRev: OFFER --> account:"
            << " uNode=" << uNode
            << " saRevDeliver=" << pnCur.saRevDeliver;

        terResult   = calcNodeDeliverRev (
            uNode,
            psCur,
            bMultiQuality,
            pnNxt.uAccountID,

            // The next node wants the current node to deliver this much:
            pnCur.saRevDeliver,
            saDeliverAct);
    }
    else
    {
        WriteLog (lsTRACE, RippleCalc)
            << "calcNodeOfferRev: OFFER --> offer: uNode=" << uNode;

        // Next is an offer. Deliver has already been resolved.
        terResult   = tesSUCCESS;
    }

    return terResult;
}

} // ripple
