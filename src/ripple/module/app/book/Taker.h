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

#ifndef RIPPLE_CORE_TAKER_H_INCLUDED
#define RIPPLE_CORE_TAKER_H_INCLUDED

#include <ripple/module/app/book/Amounts.h>
#include <ripple/module/app/book/Quality.h>
#include <ripple/module/app/book/Offer.h>
#include <ripple/module/app/book/Types.h>

#include <beast/streams/debug_ostream.h>
#include <beast/utility/noexcept.h>

#include <functional>
//#include <utility>

namespace ripple {
namespace core {

/** State for the active party during order book or payment operations. */
class Taker
{
public:
    struct Options
    {
        Options() = delete;

        explicit
        Options (std::uint32_t tx_flags)
            : sell (tx_flags & tfSell)
            , passive (tx_flags & tfPassive)
            , fill_or_kill (tx_flags & tfFillOrKill)
            , immediate_or_cancel (tx_flags & tfImmediateOrCancel)
        {
        }

        bool const sell;
        bool const passive;
        bool const fill_or_kill;
        bool const immediate_or_cancel;
    };

private:
    std::reference_wrapper <LedgerView> m_view;
    Account m_account;
    Options m_options;
    Quality m_quality;
    Quality m_threshold;

    // The original in and out quantities.
    Amounts const m_amount;

    // The amounts still left over for us to try and take.
    Amounts m_remain;

private:
    Amounts
    flow (Amounts amount, Offer const& offer, Account const& taker);

    TER
    fill (Offer const& offer, Amounts const& amount);

    TER
    fill (Offer const& leg1, Amounts const& amount1,
        Offer const& leg2, Amounts const& amount2);

public:
    Taker (LedgerView& view, Account const& account,
        Amounts const& amount, Options const& options);

    LedgerView&
    view () const noexcept
    {
        return m_view;
    }

    /** Returns the amount remaining on the offer.
        This is the amount at which the offer should be placed. It may either
        be for the full amount when there were no crossing offers, or for zero
        when the offer fully crossed, or any amount in between.
        It is always at the original offer quality (m_quality)
    */
    Amounts
    remaining_offer () const;

    /** Returns the account identifier of the taker. */
    Account const&
    account () const noexcept
    {
        return m_account;
    }

    /** Returns `true` if the quality does not meet the taker's requirements. */
    bool
    reject (Quality const& quality) const noexcept
    {
        return quality < m_threshold;
    }

    /** Returns `true` if order crossing should not continue.
        Order processing is stopped if the taker's order quantities have
        been reached, or if the taker has run out of input funds.
    */
    bool
    done () const;

    /** Perform direct crossing through given offer.
        @return tesSUCCESS on success, error code otherwise.
    */
    TER
    cross (Offer const& offer);

    /** Perform bridged crossing through given offers.
        @return tesSUCCESS on success, error code otherwise.
    */
    TER
    cross (Offer const& leg1, Offer const& leg2);
};

inline
std::ostream&
operator<< (std::ostream& os, Taker const& taker)
{
    return os << taker.account();
}

}
}

#endif
