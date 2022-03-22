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
#include <optional>

namespace ripple {

/** Describes how an account was found in a path, and how to find the next set
of paths. "Outgoing" is defined as the source account, or an account found via a
trustline that has rippling enabled on the account's side.
"Incoming" is defined as an account found via a trustline that has rippling
disabled on the account's side. Any trust lines for an incoming account that
have rippling disabled are unusable in paths.
*/
enum class LineDirection : bool { incoming = false, outgoing = true };

/** Wraps a trust line SLE for convenience.
    The complication of trust lines is that there is a
    "low" account and a "high" account. This wraps the
    SLE and expresses its data from the perspective of
    a chosen account on the line.

    This wrapper is primarily used in the path finder and there can easily be
    tens of millions of instances of this class. When modifying this class think
    carefully about the memory implications.
*/
class TrustLineBase
{
protected:
    // This class should not be instantiated directly. Use one of the derived
    // classes.
    TrustLineBase(
        std::shared_ptr<SLE const> const& sle,
        AccountID const& viewAccount);

    ~TrustLineBase() = default;
    TrustLineBase(TrustLineBase const&) = default;
    TrustLineBase&
    operator=(TrustLineBase const&) = delete;
    TrustLineBase(TrustLineBase&&) = default;

public:
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

    LineDirection
    getDirection() const
    {
        return getNoRipple() ? LineDirection::incoming
                             : LineDirection::outgoing;
    }

    LineDirection
    getDirectionPeer() const
    {
        return getNoRipplePeer() ? LineDirection::incoming
                                 : LineDirection::outgoing;
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

    std::optional<STAmount> const&
    getLockedBalance() const
    {
        return mLockedBalance;
    }

    std::optional<uint32_t> const&
    getLockCount() const
    {
        return mLockCount;
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

    Json::Value
    getJson(int);

protected:
    uint256 key_;

    STAmount const mLowLimit;
    STAmount const mHighLimit;

    STAmount mBalance;
    std::optional<STAmount> mLockedBalance;
    std::optional<uint32_t> mLockCount;  // RH NOTE: this is from sfLockCount
                                         // has nothing to do with a mutex.

    std::uint32_t mFlags;

    bool mViewLowest;
};

// This wrapper is used for the path finder
class PathFindTrustLine final : public TrustLineBase,
                                public CountedObject<PathFindTrustLine>
{
    using TrustLineBase::TrustLineBase;

public:
    PathFindTrustLine() = delete;

    static std::optional<PathFindTrustLine>
    makeItem(AccountID const& accountID, std::shared_ptr<SLE const> const& sle);

    static std::vector<PathFindTrustLine>
    getItems(
        AccountID const& accountID,
        ReadView const& view,
        LineDirection direction);
};

// This wrapper is used for the `AccountLines` command and includes the quality
// in and quality out values.
class RPCTrustLine final : public TrustLineBase,
                           public CountedObject<RPCTrustLine>
{
    using TrustLineBase::TrustLineBase;

public:
    RPCTrustLine() = delete;

    RPCTrustLine(
        std::shared_ptr<SLE const> const& sle,
        AccountID const& viewAccount);

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

    static std::optional<RPCTrustLine>
    makeItem(AccountID const& accountID, std::shared_ptr<SLE const> const& sle);

    static std::vector<RPCTrustLine>
    getItems(AccountID const& accountID, ReadView const& view);

private:
    Rate lowQualityIn_;
    Rate lowQualityOut_;
    Rate highQualityIn_;
    Rate highQualityOut_;
};

}  // namespace ripple

#endif
