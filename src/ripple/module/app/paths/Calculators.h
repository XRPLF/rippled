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

#ifndef RIPPLE_PATHS_CALCULATORS_H
#define RIPPLE_PATHS_CALCULATORS_H

#include <boost/log/trivial.hpp>

#include <ripple/module/app/paths/CalcState.h>
#include <ripple/module/app/paths/QualityConstraint.h>
#include <ripple/module/app/paths/RippleCalc.h>
#include <ripple/module/app/paths/Tuning.h>

namespace ripple {

/** RippleCalc calculates the quality of a payment path.

    Quality is the amount of input required to produce a given output along a
    specified path - another name for this is exchange rate.
*/
class RippleCalc
{
public:
    LedgerEntrySet& mActiveLedger;
    bool mOpenLedger;

    // First time working in reverse a funding source was mentioned.  Source may
    // only be used there.

    // Map of currency, issuer to node index.
    AccountCurrencyIssuerToNodeIndex mumSource;

    // If the transaction fails to meet some constraint, still need to delete
    // unfunded offers.
    //
    // Offers that were found unfunded.
    unordered_set<uint256> mUnfundedOffers;

    RippleCalc (LedgerEntrySet& activeLedger, const bool bOpenLedger)
        : mActiveLedger (activeLedger), mOpenLedger (bOpenLedger)
    {
    }
};

namespace path {

void pathNext (
    RippleCalc&,
    PathState& pathState, const bool bMultiQuality,
    const LedgerEntrySet& lesCheckpoint, LedgerEntrySet& lesCurrent);

// The next section contains functions that compute the liqudity along a path,
// either backward or forward.
//
// We need to do these computations twice - once backward to figure out the
// maximum possible liqiudity along a path, and then forward to compute the
// actual liquidity of the paths we actually chose.
//
// These functions were originally methods on RippleCalc, but will end up as
// methods on another class which keeps the state that's currently passed around
// as many parameters.  For the moment, they're free functions that take a
// RippleCalc (the path calculation "God object").
//
// Many of these routines use recursion to loop over all nodes in a path.
// TODO(tom): replace this recursion with a loop.

TER computeReverseLiqudity (
    RippleCalc&,
    unsigned int nodeIndex, PathState& pathState, bool bMultiQuality);
TER computeForwardLiqudity (
    RippleCalc&,
    unsigned int nodeIndex, PathState& pathState, bool bMultiQuality);
TER computeReverseLiquidityForOffer (
    RippleCalc&,
    unsigned int nodeIndex, PathState& pathState, bool bMultiQuality);
TER computeForwardLiquidityForOffer (
    RippleCalc&,
    unsigned int nodeIndex, PathState& pathState, bool bMultiQuality);
TER computeReverseLiquidityForAccount (
    RippleCalc&,
    unsigned int nodeIndex, PathState& pathState, bool bMultiQuality);
TER computeForwardLiquidityForAccount (
    RippleCalc&,
    unsigned int nodeIndex, PathState& pathState, bool bMultiQuality);

void computeRippleLiquidity (
    RippleCalc&,
    const std::uint32_t uQualityIn,
    const std::uint32_t uQualityOut,
    const STAmount& saPrvReq,
    const STAmount& saCurReq,
    STAmount& saPrvAct,
    STAmount& saCurAct,
    std::uint64_t& uRateMax);

// To send money out of an account.
TER nodeAdvance (
    RippleCalc&,
    unsigned int nodeIndex, PathState& pathState, bool bMultiQuality,
    bool bReverse);

// To deliver from an order book, when computing
TER nodeDeliverRev (
    RippleCalc&,
    const unsigned int          nodeIndex,
    PathState&                  pathState,
    const bool                  bMultiQuality,
    const uint160&              uOutAccountID,
    const STAmount&             saOutReq,
    STAmount&                   saOutAct);

TER nodeDeliverFwd (
    RippleCalc&,
    const unsigned int          nodeIndex,
    PathState&                  pathState,
    const bool                  bMultiQuality,
    const uint160&              uInAccountID,
    const STAmount&             saInReq,
    STAmount&                   saInAct,
    STAmount&                   saInFees);

} // path
} // ripple

#endif
