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

#ifndef RIPPLE_RIPPLECALC_H
#define RIPPLE_RIPPLECALC_H

namespace ripple {

/** Calculate the quality of a payment path.

    The quality is a synonym for price. Specifically, the amount of
    input required to produce a given output along a specified path.
*/
// VFALCO TODO What's the difference between a RippleState versus PathState?
//
class RippleCalc
{
public:
    // First time working in reverse a funding source was mentioned.  Source may only be used there.
    curIssuerNode                   mumSource;          // Map of currency, issuer to node index.

    // If the transaction fails to meet some constraint, still need to delete unfunded offers.
    boost::unordered_set<uint256>   musUnfundedFound;   // Offers that were found unfunded.

    void                pathNext (PathState::ref psrCur, const bool bMultiQuality, const LedgerEntrySet& lesCheckpoint, LedgerEntrySet& lesCurrent);
    TER                 calcNode (const unsigned int uNode, PathState& psCur, const bool bMultiQuality);
    TER                 calcNodeRev (const unsigned int uNode, PathState& psCur, const bool bMultiQuality);
    TER                 calcNodeFwd (const unsigned int uNode, PathState& psCur, const bool bMultiQuality);
    TER                 calcNodeOfferRev (const unsigned int uNode, PathState& psCur, const bool bMultiQuality);
    TER                 calcNodeOfferFwd (const unsigned int uNode, PathState& psCur, const bool bMultiQuality);
    TER                 calcNodeAccountRev (const unsigned int uNode, PathState& psCur, const bool bMultiQuality);
    TER                 calcNodeAccountFwd (const unsigned int uNode, PathState& psCur, const bool bMultiQuality);
    TER                 calcNodeAdvance (const unsigned int uNode, PathState& psCur, const bool bMultiQuality, const bool bReverse);
    TER                 calcNodeDeliverRev (
        const unsigned int          uNode,
        PathState&                  psCur,
        const bool                  bMultiQuality,
        const uint160&              uOutAccountID,
        const STAmount&             saOutReq,
        STAmount&                   saOutAct);

    TER                 calcNodeDeliverFwd (
        const unsigned int          uNode,
        PathState&                  psCur,
        const bool                  bMultiQuality,
        const uint160&              uInAccountID,
        const STAmount&             saInReq,
        STAmount&                   saInAct,
        STAmount&                   saInFees);

    void                calcNodeRipple (const std::uint32_t uQualityIn, const std::uint32_t uQualityOut,
                                        const STAmount& saPrvReq, const STAmount& saCurReq,
                                        STAmount& saPrvAct, STAmount& saCurAct,
                                        std::uint64_t& uRateMax);

    RippleCalc (LedgerEntrySet& lesNodes, const bool bOpenLedger)
        : lesActive (lesNodes), mOpenLedger (bOpenLedger)
    {
        ;
    }

    static TER rippleCalc (
        LedgerEntrySet&                 lesActive,
        STAmount&                 saMaxAmountAct,
        STAmount&                 saDstAmountAct,
        std::vector<PathState::pointer>&  vpsExpanded,
        const STAmount&                 saDstAmountReq,
        const STAmount&                 saMaxAmountReq,
        const uint160&                  uDstAccountID,
        const uint160&                  uSrcAccountID,
        const STPathSet&                spsPaths,
        const bool                      bPartialPayment,
        const bool                      bLimitQuality,
        const bool                      bNoRippleDirect,
        const bool                      bStandAlone,        // --> True, not to affect accounts.
        const bool                      bOpenLedger = true  // --> What kind of errors to return.
    );

    static void setCanonical (STPathSet& spsDst, const std::vector<PathState::pointer>& vpsExpanded, bool bKeepDefault);

protected:
    LedgerEntrySet&                 lesActive;
    bool                            mOpenLedger;
};

} // ripple

#endif
