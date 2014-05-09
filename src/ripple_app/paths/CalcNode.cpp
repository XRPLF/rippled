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

TER RippleCalc::calcNodeFwd (
    const unsigned int uNode, PathState& psCur, const bool bMultiQuality)
{
    const PathState::Node& pnCur = psCur.vpnNodes[uNode];
    const bool bCurAccount
        = is_bit_set (pnCur.uFlags,  STPathElement::typeAccount);

    WriteLog (lsTRACE, RippleCalc)
        << "calcNodeFwd> uNode=" << uNode;

    TER terResult = bCurAccount
        ? calcNodeAccountFwd (uNode, psCur, bMultiQuality)
        : calcNodeOfferFwd (uNode, psCur, bMultiQuality);

    if (tesSUCCESS == terResult && uNode + 1 != psCur.vpnNodes.size ())
        terResult   = calcNodeFwd (uNode + 1, psCur, bMultiQuality);

    if (tesSUCCESS == terResult && (!psCur.saInPass || !psCur.saOutPass))
        terResult = tecPATH_DRY;

    WriteLog (lsTRACE, RippleCalc)
        << "calcNodeFwd<"
        << " uNode:" << uNode
        << " terResult:" << terResult;

    return terResult;
}

// Calculate a node and its previous nodes.
//
// From the destination work in reverse towards the source calculating how much
// must be asked for.
//
// Then work forward, figuring out how much can actually be delivered.
// <-- terResult: tesSUCCESS or tecPATH_DRY
// <-> pnNodes:
//     --> [end]saWanted.mAmount
//     --> [all]saWanted.mCurrency
//     --> [all]saAccount
//     <-> [0]saWanted.mAmount : --> limit, <-- actual
TER RippleCalc::calcNodeRev (
    const unsigned int uNode, PathState& psCur, const bool bMultiQuality)
{
    PathState::Node& pnCur = psCur.vpnNodes[uNode];
    bool const bCurAccount
        = is_bit_set (pnCur.uFlags,  STPathElement::typeAccount);
    TER terResult;

    // Do current node reverse.
    const uint160&  uCurIssuerID    = pnCur.uIssuerID;
    STAmount& saTransferRate  = pnCur.saTransferRate;

    saTransferRate = STAmount::saFromRate (
        mActiveLedger.rippleTransferRate (uCurIssuerID));

    WriteLog (lsTRACE, RippleCalc)
        << "calcNodeRev>"
        << " uNode=" << uNode
        << " bCurAccount=" << bCurAccount
        << " uIssuerID=" << RippleAddress::createHumanAccountID (uCurIssuerID)
        << " saTransferRate=" << saTransferRate;

    terResult = bCurAccount
        ? calcNodeAccountRev (uNode, psCur, bMultiQuality)
        : calcNodeOfferRev (uNode, psCur, bMultiQuality);

    // Do previous.
    if (tesSUCCESS != terResult)
    {
        // Error, don't continue.
        nothing ();
    }
    else if (uNode)
    {
        // Continue in reverse.
        terResult = calcNodeRev (uNode - 1, psCur, bMultiQuality);
    }

    WriteLog (lsTRACE, RippleCalc)
        << "calcNodeRev< "
        << "uNode=" << uNode
        << " terResult=%s" << transToken (terResult)
        << "/" << terResult;

    return terResult;
}

} // ripple
