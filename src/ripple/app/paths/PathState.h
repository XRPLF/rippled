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

#ifndef RIPPLE_APP_PATHS_PATHSTATE_H_INCLUDED
#define RIPPLE_APP_PATHS_PATHSTATE_H_INCLUDED

#include <ripple/app/paths/Node.h>
#include <ripple/app/paths/Types.h>
#include <ripple/ledger/PaymentSandbox.h>
#include <boost/optional.hpp>

namespace ripple {

// Holds a single path state under incremental application.
class PathState : public CountedObject <PathState>
{
  public:
    using OfferIndexList = std::vector<uint256>;
    using Ptr = std::shared_ptr<PathState>;
    using List = std::vector<Ptr>;

    PathState (PaymentSandbox const& parent,
            STAmount const& saSend,
               STAmount const& saSendMax,
                   beast::Journal j)
        : mIndex (0)
        , uQuality (0)
        , saInReq (saSendMax)
        , saOutReq (saSend)
        , j_ (j)
    {
        view_.emplace(&parent);
    }

    void reset(STAmount const& in, STAmount const& out);

    TER expandPath (
        STPath const&    spSourcePath,
        AccountID const& uReceiverID,
        AccountID const& uSenderID
    );

    path::Node::List& nodes() { return nodes_; }

    STAmount const& inPass() const { return saInPass; }
    STAmount const& outPass() const { return saOutPass; }
    STAmount const& outReq() const { return saOutReq; }

    STAmount const& inAct() const { return saInAct; }
    STAmount const& outAct() const { return saOutAct; }
    STAmount const& inReq() const { return saInReq; }

    void setInPass(STAmount const& sa)
    {
        saInPass = sa;
    }

    void setOutPass(STAmount const& sa)
    {
        saOutPass = sa;
    }

    AccountIssueToNodeIndex const& forward() { return umForward; }
    AccountIssueToNodeIndex const& reverse() { return umReverse; }

    void insertReverse (AccountIssue const& ai, path::NodeIndex i)
    {
        umReverse.insert({ai, i});
    }

    static char const* getCountedObjectName () { return "PathState"; }
    OfferIndexList& unfundedOffers() { return unfundedOffers_; }

    void setStatus(TER status) { terStatus = status; }
    TER status() const { return terStatus; }

    std::uint64_t quality() const { return uQuality; }
    void setQuality (std::uint64_t q) { uQuality = q; }

    void setIndex (int i) { mIndex  = i; }
    int index() const { return mIndex; }

    TER checkNoRipple (AccountID const& destinationAccountID,
                       AccountID const& sourceAccountID);
    void checkFreeze ();

    static bool lessPriority (PathState const& lhs, PathState const& rhs);

    PaymentSandbox&
    view()
    {
        return *view_;
    }

    void resetView (PaymentSandbox const& view)
    {
        view_.emplace(&view);
    }

    bool isDry() const
    {
        return !(saInPass && saOutPass);
    }

private:
    TER checkNoRipple (
        AccountID const&, AccountID const&, AccountID const&, Currency const&);

    /** Clear path structures, and clear each node. */
    void clear();

    TER pushNode (
        int const iType,
        AccountID const& account,
        Currency const& currency,
        AccountID const& issuer);

    TER pushImpliedNodes (
        AccountID const& account,
        Currency const& currency,
        AccountID const& issuer);

    Json::Value getJson () const;

private:
    boost::optional<PaymentSandbox> view_;

    int                         mIndex;    // Index/rank amoung siblings.
    std::uint64_t               uQuality;  // 0 = no quality/liquity left.

    STAmount const&             saInReq;   // --> Max amount to spend by sender.
    STAmount                    saInAct;   // --> Amount spent by sender so far.
    STAmount                    saInPass;  // <-- Amount spent by sender.

    STAmount const&             saOutReq;  // --> Amount to send.
    STAmount                    saOutAct;  // --> Amount actually sent so far.
    STAmount                    saOutPass; // <-- Amount actually sent.

    TER terStatus;

    path::Node::List nodes_;

    // When processing, don't want to complicate directory walking with
    // deletion.  Offers that became unfunded or were completely consumed go
    // here and are deleted at the end.
    OfferIndexList unfundedOffers_;

    // First time scanning foward, as part of path construction, a funding
    // source was mentioned for accounts. Source may only be used there.
    AccountIssueToNodeIndex umForward;

    // First time working in reverse a funding source was used.
    // Source may only be used there if not mentioned by an account.
    AccountIssueToNodeIndex umReverse;

    beast::Journal const j_;
};

} // ripple

#endif
