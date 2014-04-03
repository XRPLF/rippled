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

#ifndef RIPPLE_PATHSTATE_H
#define RIPPLE_PATHSTATE_H

namespace ripple {

// account id, currency id, issuer id :: node
typedef std::tuple <uint160, uint160, uint160> aciSource;
typedef ripple::unordered_map <aciSource, unsigned int>                   curIssuerNode;  // Map of currency, issuer to node index.
typedef ripple::unordered_map <aciSource, unsigned int>::const_iterator   curIssuerNodeConstIterator;

extern std::size_t hash_value (const aciSource& asValue);

// Holds a path state under incremental application.
class PathState : public CountedObject <PathState>
{
public:

    static char const* getCountedObjectName () { return "PathState"; }

    class Node
    {
    public:
        bool operator == (Node const& pnOther) const;

        Json::Value                     getJson () const;

    public:
        std::uint16_t                   uFlags;             // --> From path.

        uint160                         uAccountID;         // --> Accounts: Recieving/sending account.
        uint160                         uCurrencyID;        // --> Accounts: Receive and send, Offers: send.
        // --- For offer's next has currency out.
        uint160                         uIssuerID;          // --> Currency's issuer

        STAmount                        saTransferRate;     // Transfer rate for uIssuerID.

        // Computed by Reverse.
        STAmount                        saRevRedeem;        // <-- Amount to redeem to next.
        STAmount                        saRevIssue;         // <-- Amount to issue to next limited by credit and outstanding IOUs.
        //     Issue isn't used by offers.
        STAmount                        saRevDeliver;       // <-- Amount to deliver to next regardless of fee.

        // Computed by forward.
        STAmount                        saFwdRedeem;        // <-- Amount node will redeem to next.
        STAmount                        saFwdIssue;         // <-- Amount node will issue to next.
        //     Issue isn't used by offers.
        STAmount                        saFwdDeliver;       // <-- Amount to deliver to next regardless of fee.

        // For offers:

        STAmount                        saRateMax;

        // Directory
        uint256                         uDirectTip;         // Current directory.
        uint256                         uDirectEnd;         // Next order book.
        bool                            bDirectAdvance;     // Need to advance directory.
        bool                            bDirectRestart;     // Need to restart directory.
        SLE::pointer                    sleDirectDir;
        STAmount                        saOfrRate;          // For correct ratio.

        // PaymentNode
        bool                            bEntryAdvance;      // Need to advance entry.
        unsigned int                    uEntry;
        uint256                         uOfferIndex;
        SLE::pointer                    sleOffer;
        uint160                         uOfrOwnerID;
        bool                            bFundsDirty;        // Need to refresh saOfferFunds, saTakerPays, & saTakerGets.
        STAmount                        saOfferFunds;
        STAmount                        saTakerPays;
        STAmount                        saTakerGets;

    };
public:
    typedef boost::shared_ptr<PathState>        pointer;
    typedef const boost::shared_ptr<PathState>& ref;

public:
    PathState*  setIndex (const int iIndex)
    {
        mIndex  = iIndex;

        return this;
    }

    int getIndex ()
    {
        return mIndex;
    };

    PathState (
        const STAmount&         saSend,
        const STAmount&         saSendMax)
        : saInReq (saSendMax)
        , saOutReq (saSend)
    {
    }

    PathState (const PathState& psSrc,
               bool bUnused)
        : saInReq (psSrc.saInReq)
        , saOutReq (psSrc.saOutReq)
    {
    }

    void setExpanded (
        const LedgerEntrySet&   lesSource,
        const STPath&           spSourcePath,
        const uint160&          uReceiverID,
        const uint160&          uSenderID
    );

    void checkNoRipple (uint160 const& destinationAccountID, uint160 const& sourceAccountID);
    void checkNoRipple (uint160 const&, uint160 const&, uint160 const&, uint160 const&);

    void setCanonical (
        const PathState&        psExpanded
    );

    Json::Value getJson () const;

#if 0
    static PathState::pointer createCanonical (
        PathState& ref       pspExpanded
    )
    {
        PathState::pointer  pspNew  = boost::make_shared<PathState> (pspExpanded->saOutAct, pspExpanded->saInAct);

        pspNew->setCanonical (pspExpanded);

        return pspNew;
    }
#endif
    static bool lessPriority (PathState& lhs, PathState& rhs);

public:
    TER                  terStatus;
    std::vector<Node>    vpnNodes;

    // When processing, don't want to complicate directory walking with deletion.
    std::vector<uint256>        vUnfundedBecame;    // Offers that became unfunded or were completely consumed.

    // First time scanning foward, as part of path contruction, a funding source was mentioned for accounts. Source may only be
    // used there.
    curIssuerNode               umForward;          // Map of currency, issuer to node index.

    // First time working in reverse a funding source was used.
    // Source may only be used there if not mentioned by an account.
    curIssuerNode               umReverse;          // Map of currency, issuer to node index.

    LedgerEntrySet              lesEntries;

    int                         mIndex;             // Index/rank amoung siblings.
    std::uint64_t               uQuality;           // 0 = no quality/liquity left.
    const STAmount&             saInReq;            // --> Max amount to spend by sender.
    STAmount                    saInAct;            // --> Amount spent by sender so far.
    STAmount                    saInPass;           // <-- Amount spent by sender.
    const STAmount&             saOutReq;           // --> Amount to send.
    STAmount                    saOutAct;           // --> Amount actually sent so far.
    STAmount                    saOutPass;          // <-- Amount actually sent.
    bool                        bConsumed;          // If true, use consumes full liquidity. False, may or may not.

private:
    TER pushNode (const int iType, const uint160& uAccountID, const uint160& uCurrencyID, const uint160& uIssuerID);
    TER pushImply (const uint160& uAccountID, const uint160& uCurrencyID, const uint160& uIssuerID);
};

} // ripple

#endif
