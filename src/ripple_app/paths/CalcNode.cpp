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

#include <tuple>

#include <ripple_app/paths/CalcState.h>
#include <ripple_app/paths/Calculators.h>
#include <ripple_app/paths/RippleCalc.h>
#include <ripple_app/paths/Tuning.h>

namespace ripple {
namespace path {

TER nodeFwd (
    RippleCalc& rippleCalc,
    const unsigned int nodeIndex, PathState& pathState,
    const bool bMultiQuality)
{
    auto const& node = pathState.nodes()[nodeIndex];
    auto const isAccount = node.isAccount();

    WriteLog (lsTRACE, RippleCalc)
        << "nodeFwd> nodeIndex=" << nodeIndex;

    TER resultCode = isAccount
        ? nodeAccountFwd (rippleCalc, nodeIndex, pathState, bMultiQuality)
        : nodeOfferFwd (rippleCalc, nodeIndex, pathState, bMultiQuality);

    if (resultCode == tesSUCCESS && nodeIndex + 1 != pathState.nodes().size ())
        resultCode = nodeFwd (rippleCalc, nodeIndex + 1, pathState, bMultiQuality);

    if (resultCode == tesSUCCESS && !(pathState.inPass() && pathState.outPass()))
        resultCode = tecPATH_DRY;

    WriteLog (lsTRACE, RippleCalc)
        << "nodeFwd<"
        << " nodeIndex:" << nodeIndex
        << " resultCode:" << resultCode;

    return resultCode;
}

// Calculate a node and its previous nodes.
//
// From the destination work in reverse towards the source calculating how much
// must be asked for.
//
// Then work forward, figuring out how much can actually be delivered.
// <-- resultCode: tesSUCCESS or tecPATH_DRY
// <-> pnNodes:
//     --> [end]saWanted.mAmount
//     --> [all]saWanted.mCurrency
//     --> [all]saAccount
//     <-> [0]saWanted.mAmount : --> limit, <-- actual
TER nodeRev (
    RippleCalc& rippleCalc,
    const unsigned int nodeIndex, PathState& pathState,
    const bool bMultiQuality)
{
    auto& node = pathState.nodes()[nodeIndex];
    auto const isAccount = node.isAccount();
    TER resultCode;

    node.saTransferRate = STAmount::saFromRate (
        rippleCalc.mActiveLedger.rippleTransferRate (node.uIssuerID));

    WriteLog (lsTRACE, RippleCalc)
        << "nodeRev>"
        << " nodeIndex=" << nodeIndex
        << " isAccount=" << isAccount
        << " uIssuerID=" << RippleAddress::createHumanAccountID (node.uIssuerID)
        << " saTransferRate=" << node.saTransferRate;

    resultCode = isAccount
            ? nodeAccountRev (rippleCalc, nodeIndex, pathState, bMultiQuality)
        : nodeOfferRev (rippleCalc, nodeIndex, pathState, bMultiQuality);

    // Do previous.
    if (resultCode != tesSUCCESS)
        // Error, don't continue.
        nothing ();
    else if (nodeIndex)
        // Continue in reverse.  TODO(tom): remove unnecessary recursion.
        resultCode = nodeRev (rippleCalc, nodeIndex - 1, pathState, bMultiQuality);

    WriteLog (lsTRACE, RippleCalc)
        << "nodeRev< "
        << "nodeIndex=" << nodeIndex
        << " resultCode=%s" << transToken (resultCode)
        << "/" << resultCode;

    return resultCode;
}

} // path
} // ripple
