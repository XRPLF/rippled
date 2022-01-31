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

#ifndef RIPPLE_APP_PATHS_RIPPLESTATE_H_INCLUDED
#define RIPPLE_APP_PATHS_RIPPLESTATE_H_INCLUDED

#include <ripple/basics/CountedObject.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Rate.h>
#include <ripple/protocol/STAmount.h>
#include <ripple/protocol/STLedgerEntry.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace ripple {

/** Wraps a trust line SLE for convenience.
    The complication of trust lines is that there is a
    "low" account and a "high" account. This wraps the
    SLE and expresses its data from the perspective of
    a chosen account on the line.
*/
// VFALCO TODO Rename to TrustLine
class RippleState final : public CountedObject<RippleState>
{
public:
    RippleState() = delete;

    static std::optional<RippleState>
    makeItem(AccountID const& accountID, std::shared_ptr<SLE const> const& sle);

    // Must be public, for make_shared
    RippleState(
        std::shared_ptr<SLE const> const& sle,
        AccountID const& viewAccount);

    /** Returns the state map key for the ledger entry. */
    uint256 const&
    key() const
    {
        return key_;
    }

    // VFALCO Take off the "get" from each function name

    AccountID const&
    getAccountID() const
    {
        return mViewLowest ? mLowLimit.getIssuer() : mHighLimit.getIssuer();
    }

    AccountID const&
    getAccountIDPeer() const
    {
        return !mViewLowest ? mLowLimit.getIssuer() : mHighLimit.getIssuer();
    }

    // True, Provided auth to peer.
    bool
    getAuth() const
    {
        return mFlags & (mViewLowest ? lsfLowAuth : lsfHighAuth);
    }

    bool
    getAuthPeer() const
    {
        return mFlags & (!mViewLowest ? lsfLowAuth : lsfHighAuth);
    }

    bool
    getDefaultRipple() const
    {
        return mFlags & lsfDefaultRipple;
    }

    bool
    getNoRipple() const
    {
        return mFlags & (mViewLowest ? lsfLowNoRipple : lsfHighNoRipple);
    }

    bool
    getNoRipplePeer() const
    {
        return mFlags & (!mViewLowest ? lsfLowNoRipple : lsfHighNoRipple);
    }

    /** Have we set the freeze flag on our peer */
    bool
    getFreeze() const
    {
        return mFlags & (mViewLowest ? lsfLowFreeze : lsfHighFreeze);
    }

    /** Has the peer set the freeze flag on us */
    bool
    getFreezePeer() const
    {
        return mFlags & (!mViewLowest ? lsfLowFreeze : lsfHighFreeze);
    }

    STAmount const&
    getBalance() const
    {
        return mBalance;
    }

    STAmount const&
    getLimit() const
    {
        return mViewLowest ? mLowLimit : mHighLimit;
    }

    STAmount const&
    getLimitPeer() const
    {
        return !mViewLowest ? mLowLimit : mHighLimit;
    }

    Rate const&
    getQualityIn() const
    {
        return mViewLowest ? lowQualityIn_ : highQualityIn_;
    }

    Rate const&
    getQualityOut() const
    {
        return mViewLowest ? lowQualityOut_ : highQualityOut_;
    }

    Json::Value
    getJson(int);

private:
    uint256 key_;

    STAmount const mLowLimit;
    STAmount const mHighLimit;

    STAmount mBalance;

    Rate lowQualityIn_;
    Rate lowQualityOut_;
    Rate highQualityIn_;
    Rate highQualityOut_;

    std::uint32_t mFlags;

    bool mViewLowest;
};

std::vector<RippleState>
getRippleStateItems(AccountID const& accountID, ReadView const& view);

}  // namespace ripple

#endif
