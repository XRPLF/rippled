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
    const unsigned int nodeIndex, PathState& pathState,
    const bool bMultiQuality)
{
    const PathState::Node& node = pathState.vpnNodes[nodeIndex];
    const bool nodeIsAccount
        = is_bit_set (node.uFlags,  STPathElement::typeAccount);

    WriteLog (lsTRACE, RippleCalc)
        << "calcNodeFwd> nodeIndex=" << nodeIndex;

    TER errorCode = nodeIsAccount
        ? calcNodeAccountFwd (nodeIndex, pathState, bMultiQuality)
        : calcNodeOfferFwd (nodeIndex, pathState, bMultiQuality);

    if (errorCode == tesSUCCESS && nodeIndex + 1 != pathState.vpnNodes.size ())
        errorCode = calcNodeFwd (nodeIndex + 1, pathState, bMultiQuality);

    if (errorCode == tesSUCCESS && !(pathState.saInPass && pathState.saOutPass))
        errorCode = tecPATH_DRY;

    WriteLog (lsTRACE, RippleCalc)
        << "calcNodeFwd<"
        << " nodeIndex:" << nodeIndex
        << " errorCode:" << errorCode;

    return errorCode;
}

// Calculate a node and its previous nodes.
//
// From the destination work in reverse towards the source calculating how much
// must be asked for.
//
// Then work forward, figuring out how much can actually be delivered.
// <-- errorCode: tesSUCCESS or tecPATH_DRY
// <-> pnNodes:
//     --> [end]saWanted.mAmount
//     --> [all]saWanted.mCurrency
//     --> [all]saAccount
//     <-> [0]saWanted.mAmount : --> limit, <-- actual
TER RippleCalc::calcNodeRev (
    const unsigned int nodeIndex, PathState& pathState,
    const bool bMultiQuality)
{
    PathState::Node& node = pathState.vpnNodes[nodeIndex];
    bool const nodeIsAccount
        = is_bit_set (node.uFlags,  STPathElement::typeAccount);
    TER errorCode;

    node.saTransferRate = STAmount::saFromRate (
        mActiveLedger.rippleTransferRate (node.uIssuerID));

    WriteLog (lsTRACE, RippleCalc)
        << "calcNodeRev>"
        << " nodeIndex=" << nodeIndex
        << " nodeIsAccount=" << nodeIsAccount
        << " uIssuerID=" << RippleAddress::createHumanAccountID (node.uIssuerID)
        << " saTransferRate=" << node.saTransferRate;

    errorCode = nodeIsAccount
        ? calcNodeAccountRev (nodeIndex, pathState, bMultiQuality)
        : calcNodeOfferRev (nodeIndex, pathState, bMultiQuality);

    // Do previous.
    if (errorCode != tesSUCCESS)
        // Error, don't continue.
        nothing ();
    else if (nodeIndex)
        // Continue in reverse.
        errorCode = calcNodeRev (nodeIndex - 1, pathState, bMultiQuality);

    WriteLog (lsTRACE, RippleCalc)
        << "calcNodeRev< "
        << "nodeIndex=" << nodeIndex
        << " errorCode=%s" << transToken (errorCode)
        << "/" << errorCode;

    return errorCode;
}

} // ripple
