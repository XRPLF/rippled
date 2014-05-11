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

namespace ripple {

// TODO(vfalco) What's the difference between a RippleState versus PathState?
//
struct RippleCalc
{
    LedgerEntrySet& mActiveLedger;
    bool mOpenLedger;
    // First time working in reverse a funding source was mentioned.  Source may
    // only be used there.
    //
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

void pathNext (
    RippleCalc&,
    PathState& pathState, const bool bMultiQuality,
    const LedgerEntrySet& lesCheckpoint, LedgerEntrySet& lesCurrent);

TER calcNodeRev (
    RippleCalc&,
    unsigned int nodeIndex, PathState& pathState, bool bMultiQuality);
TER calcNodeFwd (
    RippleCalc&,
    unsigned int nodeIndex, PathState& pathState, bool bMultiQuality);
TER calcNodeOfferRev (
    RippleCalc&,
    unsigned int nodeIndex, PathState& pathState, bool bMultiQuality);
TER calcNodeOfferFwd (
    RippleCalc&,
    unsigned int nodeIndex, PathState& pathState, bool bMultiQuality);
TER calcNodeAccountRev (
    RippleCalc&,
    unsigned int nodeIndex, PathState& pathState, bool bMultiQuality);
TER calcNodeAccountFwd (
    RippleCalc&,
    unsigned int nodeIndex, PathState& pathState, bool bMultiQuality);
TER calcNodeAdvance (
    RippleCalc&,
    unsigned int nodeIndex, PathState& pathState, bool bMultiQuality,
    bool bReverse);

TER calcNodeDeliverRev (
    RippleCalc&,
    const unsigned int          nodeIndex,
    PathState&                  pathState,
    const bool                  bMultiQuality,
    const uint160&              uOutAccountID,
    const STAmount&             saOutReq,
    STAmount&                   saOutAct);

TER calcNodeDeliverFwd (
    RippleCalc&,
    const unsigned int          nodeIndex,
    PathState&                  pathState,
    const bool                  bMultiQuality,
    const uint160&              uInAccountID,
    const STAmount&             saInReq,
    STAmount&                   saInAct,
    STAmount&                   saInFees);

void calcNodeRipple (
    RippleCalc&,
    const std::uint32_t uQualityIn, const std::uint32_t uQualityOut,
    const STAmount& saPrvReq, const STAmount& saCurReq,
    STAmount& saPrvAct, STAmount& saCurAct, std::uint64_t& uRateMax);

void setCanonical (
    STPathSet& spsDst, const std::vector<PathState::pointer>& vpsExpanded,
    bool bKeepDefault);

} // ripple

#endif
