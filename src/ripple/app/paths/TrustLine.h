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

#ifndef RIPPLE_APP_PATHS_TRUSTLINE_H_INCLUDED
#define RIPPLE_APP_PATHS_TRUSTLINE_H_INCLUDED

#include <ripple/basics/CountedObject.h>
#include <ripple/basics/IOUAmount.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/AccountID.h>
#include <ripple/protocol/Rate.h>
#include <ripple/protocol/STAmount.h>
#include <ripple/protocol/STLedgerEntry.h>

#include <cstdint>
#include <memory>
#include <type_traits>

namespace ripple {

/** How an account was found in a path, and how to find the next set of paths.
 */
enum class LineDirection : bool {
    /// An account found via a trust line that has rippling disabled on
    /// the account's side. Trust lines for incoming accounts that have
    /// rippling disabled are unusable in paths.
    incoming = false,

    /// The source account, or an account found via a trust line that
    /// has rippling enabled on the account's side.
    outgoing = true
};

/** Wraps a trust line SLE for convenience.

    The complication of trust lines is that there is a "low" account and
    a "high" account. This object wraps the trustline SLE, and expresses
    its data from the perspective of a chosen account on the line.

    This wrapper is primarily used in the path finder. Size is important
    because there might be many instances of this class; carefully think
    about the memory implications when modifying this class.
*/
namespace detail {
class TrustLineBase
{
    struct Side
    {
        IOUAmount limit;
        AccountID account;
        bool auth;
        bool frozen;
        bool rippling;
        bool reserve;
    };

protected:
    TrustLineBase(
        std::shared_ptr<SLE const> const& sle,
        AccountID const& viewAccount)
        : flags_(sle->getFieldU32(sfFlags))
        , currency_(sle->getFieldAmount(sfBalance).getCurrency())
        , self_([this, &sle]() {
            auto const& amount = sle->getFieldAmount(sfLowLimit);
            assert(!amount.native());
            assert(amount.getIssuer() != noAccount());

            return Side{
                amount.iou(),
                amount.getIssuer(),
                (flags_ & lsfLowAuth) != 0,
                (flags_ & lsfLowFreeze) != 0,
                (flags_ & lsfLowNoRipple) == 0,
                (flags_ & lsfLowReserve) != 0};
        }())
        , peer_([this, &sle]() {
            auto const& amount = sle->getFieldAmount(sfHighLimit);
            assert(!amount.native());
            assert(amount.getIssuer() != noAccount());

            return Side{
                amount.iou(),
                amount.getIssuer(),
                (flags_ & lsfHighAuth) != 0,
                (flags_ & lsfHighFreeze) != 0,
                (flags_ & lsfHighNoRipple) == 0,
                (flags_ & lsfHighReserve) != 0};
        }())
        , balance_(sle->getFieldAmount(sfBalance).iou())
    {
        // Ensure that the view is consistent: `self_` always refers to the
        // account we're viewing this line as, and the balance also reflects
        // that.
        if (self_.account != viewAccount)
        {
            std::swap(self_, peer_);
            balance_ = -balance_;
        }

        assert(self_.account == viewAccount);
    }

    // To maximize space savings this destructor is not marked as virtual
    // so, to ensure correctness, we make it protected. That way, derived
    // instances of this class cannot be deleted via pointers to the base.
    ~TrustLineBase() = default;

public:
    TrustLineBase(TrustLineBase const&) = default;
    TrustLineBase&
    operator=(TrustLineBase const&) = default;

    TrustLineBase(TrustLineBase&&) = default;
    TrustLineBase&
    operator=(TrustLineBase&&) = default;

    [[nodiscard]] Currency const&
    currency() const
    {
        return currency_;
    }

    [[nodiscard]] AccountID const&
    getAccountID() const
    {
        return self_.account;
    }

    [[nodiscard]] AccountID const&
    peerAccount() const
    {
        return peer_.account;
    }

    [[nodiscard]] bool
    getAuth() const
    {
        return self_.auth;
    }

    [[nodiscard]] bool
    getAuthPeer() const
    {
        return peer_.auth;
    }

    [[nodiscard]] bool
    getNoRipple() const
    {
        return !self_.rippling;
    }

    [[nodiscard]] bool
    getNoRipplePeer() const
    {
        return !peer_.rippling;
    }

    [[nodiscard]] LineDirection
    getDirection() const
    {
        return getNoRipple() ? LineDirection::incoming
                             : LineDirection::outgoing;
    }

    [[nodiscard]] LineDirection
    getDirectionPeer() const
    {
        return getNoRipplePeer() ? LineDirection::incoming
                                 : LineDirection::outgoing;
    }

    /** Have we set the freeze flag on our peer */
    [[nodiscard]] bool
    getFreeze() const
    {
        return self_.frozen;
    }

    /** Has the peer set the freeze flag on us */
    [[nodiscard]] bool
    getFreezePeer() const
    {
        return peer_.frozen;
    }

    [[nodiscard]] bool
    paidReserve() const
    {
        return self_.reserve;
    }

    [[nodiscard]] bool
    paidReservePeer() const
    {
        return peer_.reserve;
    }

    [[nodiscard]] STAmount
    getBalance() const
    {
        return {balance_, {currency_, noAccount()}};
    }

    [[nodiscard]] STAmount
    getLimit() const
    {
        return {self_.limit, {currency_, self_.account}};
    }

    [[nodiscard]] STAmount
    getLimitPeer() const
    {
        return {peer_.limit, {currency_, peer_.account}};
    }

    [[nodiscard]] std::uint32_t
    getFlags() const
    {
        return flags_;
    }

protected:
    // The flags set on the trustline. Careful accessing these
    // as you need to account for "low" and "high" sides.
    std::uint32_t flags_;

    // The currency code associated with this trustline.
    Currency currency_;

    // The side of the trustline we're viewing the balance from.
    Side self_;

    // The "other" side of the trustline.
    Side peer_;

    // The actual balance.
    IOUAmount balance_;
};
}  // namespace detail

// This wrapper is used for the pathfinder
class PathFindTrustLine final : public detail::TrustLineBase,
                                public CountedObject<PathFindTrustLine>
{
public:
    PathFindTrustLine(
        std::shared_ptr<SLE const> const& sle,
        AccountID const& viewAccount)
        : detail::TrustLineBase(sle, viewAccount)
    {
    }

    PathFindTrustLine() = delete;

    PathFindTrustLine(PathFindTrustLine const&) = default;
    PathFindTrustLine&
    operator=(PathFindTrustLine const&) = default;

    PathFindTrustLine(PathFindTrustLine&&) = default;
    PathFindTrustLine&
    operator=(PathFindTrustLine&&) = default;

    ~PathFindTrustLine() = default;
};

// This wrapper is used for the `AccountLines` command and includes the quality
// in and quality out values.
class RPCTrustLine final : public detail::TrustLineBase,
                           public CountedObject<RPCTrustLine>
{
public:
    RPCTrustLine(
        std::shared_ptr<SLE const> const& sle,
        AccountID const& viewAccount)
        : detail::TrustLineBase(sle, viewAccount)
        , selfQualityIn_(
              (self_.account < peer_.account)
                  ? sle->getFieldU32(sfLowQualityIn)
                  : sle->getFieldU32(sfHighQualityIn))
        , selfQualityOut_(
              (self_.account < peer_.account)
                  ? sle->getFieldU32(sfLowQualityOut)
                  : sle->getFieldU32(sfHighQualityOut))
    {
    }

    RPCTrustLine(RPCTrustLine const&) = default;
    RPCTrustLine&
    operator=(RPCTrustLine const&) = default;

    RPCTrustLine(RPCTrustLine&&) = default;
    RPCTrustLine&
    operator=(RPCTrustLine&&) = default;

    RPCTrustLine() = delete;

    [[nodiscard]] Rate const&
    getQualityIn() const
    {
        return selfQualityIn_;
    }

    [[nodiscard]] Rate const&
    getQualityOut() const
    {
        return selfQualityOut_;
    }

private:
    Rate selfQualityIn_;
    Rate selfQualityOut_;
};

template <
    class T,
    class = std::enable_if_t<
        std::is_same_v<T, RPCTrustLine> ||
        std::is_same_v<T, PathFindTrustLine>>>
std::vector<T>
getTrustLines(
    AccountID const& accountID,
    ReadView const& view,
    LineDirection direction = LineDirection::outgoing)
{
    std::vector<T> ret;
    ret.reserve(64);
    forEachItem(
        view,
        accountID,
        [&ret, &accountID, &direction](std::shared_ptr<SLE const> const& sle) {
            if (sle && sle->getType() == ltRIPPLE_STATE)
            {
                if (T const tl(sle, accountID);
                    direction == LineDirection::outgoing || !tl.getNoRipple())
                    ret.push_back(std::move(tl));
            }
        });

    return ret;
}

}  // namespace ripple

#endif
