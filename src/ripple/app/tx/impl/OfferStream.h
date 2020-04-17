//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2014 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_BOOK_OFFERSTREAM_H_INCLUDED
#define RIPPLE_APP_BOOK_OFFERSTREAM_H_INCLUDED

#include <ripple/app/tx/impl/BookTip.h>
#include <ripple/app/tx/impl/Offer.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/chrono.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Quality.h>

#include <boost/container/flat_set.hpp>

namespace ripple {

template <class TIn, class TOut>
class TOfferStreamBase
{
public:
    class StepCounter
    {
    private:
        std::uint32_t const limit_;
        std::uint32_t count_;
        beast::Journal j_;

    public:
        StepCounter(std::uint32_t limit, beast::Journal j)
            : limit_(limit), count_(0), j_(j)
        {
        }

        bool
        step()
        {
            if (count_ >= limit_)
            {
                JLOG(j_.debug()) << "Exceeded " << limit_ << " step limit.";
                return false;
            }
            count_++;
            return true;
        }
        std::uint32_t
        count() const
        {
            return count_;
        }
    };

protected:
    beast::Journal const j_;
    ApplyView& view_;
    ApplyView& cancelView_;
    Book book_;
    bool validBook_;
    NetClock::time_point const expire_;
    BookTip tip_;
    TOffer<TIn, TOut> offer_;
    boost::optional<TOut> ownerFunds_;
    StepCounter& counter_;

    void
    erase(ApplyView& view);

    virtual void
    permRmOffer(uint256 const& offerIndex) = 0;

public:
    TOfferStreamBase(
        ApplyView& view,
        ApplyView& cancelView,
        Book const& book,
        NetClock::time_point when,
        StepCounter& counter,
        beast::Journal journal);

    virtual ~TOfferStreamBase() = default;

    /** Returns the offer at the tip of the order book.
        Offers are always presented in decreasing quality.
        Only valid if step() returned `true`.
    */
    TOffer<TIn, TOut>&
    tip() const
    {
        return const_cast<TOfferStreamBase*>(this)->offer_;
    }

    /** Advance to the next valid offer.
        This automatically removes:
            - Offers with missing ledger entries
            - Offers found unfunded
            - expired offers
        @return `true` if there is a valid offer.
    */
    bool
    step();

    TOut
    ownerFunds() const
    {
        return *ownerFunds_;
    }
};

/** Presents and consumes the offers in an order book.

    Two `ApplyView` objects accumulate changes to the ledger. `view`
    is applied when the calling transaction succeeds. If the calling
    transaction fails, then `view_cancel` is applied.

    Certain invalid offers are automatically removed:
    - Offers with missing ledger entries
    - Offers that expired
    - Offers found unfunded:
    An offer is found unfunded when the corresponding balance is zero
    and the caller has not modified the balance. This is accomplished
    by also looking up the balance in the cancel view.

    When an offer is removed, it is removed from both views. This grooms the
    order book regardless of whether or not the transaction is successful.
*/
class OfferStream : public TOfferStreamBase<STAmount, STAmount>
{
protected:
    void
    permRmOffer(uint256 const& offerIndex) override;

public:
    using TOfferStreamBase<STAmount, STAmount>::TOfferStreamBase;
};

/** Presents and consumes the offers in an order book.

    The `view_' ` `ApplyView` accumulates changes to the ledger.
    The `cancelView_` is used to determine if an offer is found
    unfunded or became unfunded.
    The `permToRemove` collection identifies offers that should be
    removed even if the strand associated with this OfferStream
    is not applied.

    Certain invalid offers are added to the `permToRemove` collection:
    - Offers with missing ledger entries
    - Offers that expired
    - Offers found unfunded:
    An offer is found unfunded when the corresponding balance is zero
    and the caller has not modified the balance. This is accomplished
    by also looking up the balance in the cancel view.
*/
template <class TIn, class TOut>
class FlowOfferStream : public TOfferStreamBase<TIn, TOut>
{
private:
    boost::container::flat_set<uint256> permToRemove_;

public:
    using TOfferStreamBase<TIn, TOut>::TOfferStreamBase;

    // The following interface allows offer crossing to permanently
    // remove self crossed offers.  The motivation is somewhat
    // unintuitive.  See the discussion in the comments for
    // BookOfferCrossingStep::limitSelfCrossQuality().
    void
    permRmOffer(uint256 const& offerIndex) override;

    boost::container::flat_set<uint256> const&
    permToRemove() const
    {
        return permToRemove_;
    }
};
}  // namespace ripple

#endif
