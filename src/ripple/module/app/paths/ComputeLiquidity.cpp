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

#include <ripple/module/app/paths/CalcState.h>
#include <ripple/module/app/paths/Calculators.h>
#include <ripple/module/app/paths/RippleCalc.h>
#include <ripple/module/app/paths/Tuning.h>

namespace ripple {
namespace path {

TER computeForwardLiqudity (
    RippleCalc& rippleCalc,
    const unsigned int nodeIndex, PathState& pathState,
    const bool bMultiQuality)
{
    auto const& node = pathState.nodes()[nodeIndex];
    WriteLog (lsTRACE, RippleCalc)
        << "computeForwardLiqudity> nodeIndex=" << nodeIndex;

    TER resultCode = node.isAccount()
        ? computeForwardLiquidityForAccount (
            rippleCalc, nodeIndex, pathState, bMultiQuality)
        : computeForwardLiquidityForOffer (
              rippleCalc, nodeIndex, pathState, bMultiQuality);

    if (resultCode == tesSUCCESS && nodeIndex + 1 != pathState.nodes().size ())
        resultCode = computeForwardLiqudity (rippleCalc, nodeIndex + 1, pathState, bMultiQuality);

    if (resultCode == tesSUCCESS && !(pathState.inPass() && pathState.outPass()))
        resultCode = tecPATH_DRY;

    WriteLog (lsTRACE, RippleCalc)
        << "computeForwardLiqudity<"
        << " nodeIndex:" << nodeIndex
        << " resultCode:" << resultCode;

    return resultCode;
}

// Calculate a node and its previous nodes.  The eventual goal is to determine
// how much input currency we need in the forward direction to satisfy the
// output.
//
// From the destination work in reverse towards the source calculating how much
// must be asked for.  As we move backwards, individual nodes may further limit
// the amount of liquidity available.
//
// This is just a controlling loop that sets things up and then hands the work
// off to either computeReverseLiquidityForAccount or computeReverseLiquidityForOffer.
//
// Later on the result of this will be used to work forward, figuring out how
// much can actually be delivered.
//
// <-- resultCode: tesSUCCESS or tecPATH_DRY
// <-> pnNodes:
//     --> [end]saWanted.mAmount
//     --> [all]saWanted.mCurrency
//     --> [all]saAccount
//     <-> [0]saWanted.mAmount : --> limit, <-- actual

TER computeReverseLiqudity (
    RippleCalc& rippleCalc,
    const unsigned int nodeIndex, PathState& pathState,
    const bool bMultiQuality)
{
    auto& node = pathState.nodes()[nodeIndex];

    // Every account has a transfer rate for its issuances.

    // TOMOVE: The account charges
    // a fee when third parties transfer that account's own issuances.

    // node.transferRate_ caches the output transfer rate for this node.
    node.transferRate_ = STAmount::saFromRate (
        rippleCalc.mActiveLedger.rippleTransferRate (node.issuer_));

    WriteLog (lsTRACE, RippleCalc)
        << "computeReverseLiqudity>"
        << " nodeIndex=" << nodeIndex
        << " issuer_=" << RippleAddress::createHumanAccountID (node.issuer_)
        << " transferRate_=" << node.transferRate_;

    auto resultCode = node.isAccount()
        ? computeReverseLiquidityForAccount (
            rippleCalc, nodeIndex, pathState, bMultiQuality)
        : computeReverseLiquidityForOffer (
            rippleCalc, nodeIndex, pathState, bMultiQuality);

    // Do previous.
    if (resultCode == tesSUCCESS && nodeIndex)
    {
        // Continue in reverse.  TODO(tom): remove unnecessary recursion.
        resultCode = computeReverseLiqudity (
            rippleCalc, nodeIndex - 1, pathState, bMultiQuality);
    }

    WriteLog (lsTRACE, RippleCalc)
        << "computeReverseLiqudity< "
        << "nodeIndex=" << nodeIndex
        << " resultCode=%s" << transToken (resultCode)
        << "/" << resultCode;

    return resultCode;
}

} // path
} // ripple
