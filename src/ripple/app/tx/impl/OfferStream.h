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
#include <ripple/ledger/View.h>
#include <ripple/protocol/Quality.h>
#include <beast/utility/Journal.h>

namespace ripple {

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
class OfferStream
{
public:
    class StepCounter
    {
    private:
        std::uint32_t const limit_;
        std::uint32_t count_;
        beast::Journal j_;

    public:
        StepCounter (std::uint32_t limit, beast::Journal j)
            : limit_ (limit)
            , count_ (0)
            , j_ (j)
        {
        }

        bool
        step ()
        {
            if (count_ >= limit_)
            {
                j_.debug << "Exceeded " << limit_ << " step limit.";
                return false;
            }
            count_++;
            return true;
        }
    };

private:
    beast::Journal j_;
    ApplyView& view_;
    ApplyView& cancelView_;
    Book book_;
    Clock::time_point const expire_;
    BookTip tip_;
    Offer offer_;
    StepCounter& counter_;

    void
    erase (ApplyView& view);

public:
    OfferStream (ApplyView& view, ApplyView& cancelView,
        Book const& book, Clock::time_point when,
            StepCounter& counter, beast::Journal journal);

    /** Returns the offer at the tip of the order book.
        Offers are always presented in decreasing quality.
        Only valid if step() returned `true`.
    */
    Offer const&
    tip () const
    {
        return offer_;
    }

    /** Advance to the next valid offer.
        This automatically removes:
            - Offers with missing ledger entries
            - Offers found unfunded
            - expired offers
        @return `true` if there is a valid offer.
    */
    bool
    step (Logs& l);
};

}

#endif

