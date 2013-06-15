#ifndef RIPPLE_RIPPLECALC_H
#define RIPPLE_RIPPLECALC_H

// VFALCO TODO What's the difference between a RippleCalc versus Pathfinder?
//             Or a RippleState versus PathState?
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

    void                calcNodeRipple (const uint32 uQualityIn, const uint32 uQualityOut,
                                        const STAmount& saPrvReq, const STAmount& saCurReq,
                                        STAmount& saPrvAct, STAmount& saCurAct,
                                        uint64& uRateMax);

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

#endif
// vim:ts=4
