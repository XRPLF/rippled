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

#ifndef RIPPLE_APP_PATH_NODE_H
#define RIPPLE_APP_PATH_NODE_H

#include <ripple/module/app/paths/Types.h>

namespace ripple {
namespace path {

struct Node
{
    typedef std::vector<Node> List;

    inline bool isAccount() const
    {
        return (uFlags & STPathElement::typeAccount);
    }

    Json::Value getJson () const;

    bool operator == (Node const&) const;

    std::uint16_t uFlags;       // --> From path.

    Account account_;           // --> Accounts: Receiving/sending account.
    Currency currency_;         // --> Accounts: Receive and send, Offers: send.
                                // --- For offer's next has currency out.
    Account issuer_;            // --> Currency's issuer

    STAmount transferRate_;    // Transfer rate for issuer.

    // Computed by Reverse.
    STAmount saRevRedeem;        // <-- Amount to redeem to next.
    STAmount saRevIssue;         // <-- Amount to issue to next, limited by
                                 //     credit and outstanding IOUs.  Issue
                                 //     isn't used by offers.
    STAmount saRevDeliver;       // <-- Amount to deliver to next regardless of
                                 // fee.

    // Computed by forward.
    STAmount saFwdRedeem;        // <-- Amount node will redeem to next.
    STAmount saFwdIssue;         // <-- Amount node will issue to next.
                                 //     Issue isn't used by offers.
    STAmount saFwdDeliver;       // <-- Amount to deliver to next regardless of
                                 // fee.

    // For offers:

    STAmount saRateMax;

    // The nodes are partitioned into a buckets called "directories".
    //
    // Each "directory" contains nodes with exactly the same "quality" (meaning
    // the conversion rate between one corrency and the next).
    //
    // The "directories" are ordered in "increasing" "quality" value, which
    // means that the first "directory" has the "best" (i.e. numerically least)
    // "quality".
    // https://ripple.com/wiki/Ledger_Format#Prioritizing_a_continuous_key_space

    // Current directory - the last 64 bits of this are the quality.
    uint256 uDirectTip;

    // Start of the next order book - one past the worst quality possible for
    // the current order book.
    uint256 uDirectEnd;


    bool bDirectAdvance;        // Need to advance directory.
    bool bDirectRestart;        // Need to restart directory.
    SLE::pointer sleDirectDir;
    STAmount saOfrRate;          // For correct ratio.

    // PaymentNode
    bool bEntryAdvance;          // Need to advance entry.
    unsigned int uEntry;
    uint256 offerIndex_;
    SLE::pointer sleOffer;
    Account offerOwnerAccount_;

    // Do we need to refresh saOfferFunds, saTakerPays, & saTakerGets?
    bool bFundsDirty;
    STAmount saOfferFunds;
    STAmount saTakerPays;
    STAmount saTakerGets;
};

} // path
} // ripple

#endif
