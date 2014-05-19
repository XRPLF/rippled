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

#ifndef RIPPLE_CORE_TAKER_H_INCLUDED
#define RIPPLE_CORE_TAKER_H_INCLUDED

#include "Amounts.h"
#include "Quality.h"
#include "Types.h"

#include "../../beast/beast/streams/debug_ostream.h"

#include <utility>

namespace ripple {
namespace core {

/** State for the active party during order book or payment operations. */
class Taker
{
public:
    struct Options
    {
        Options() = default;

        explicit
        Options (std::uint32_t tx_flags)
            : sell (is_bit_set (tx_flags, tfSell))
            , passive (is_bit_set (tx_flags, tfPassive))
            , fill_or_kill (is_bit_set (tx_flags, tfFillOrKill))
            , immediate_or_cancel (is_bit_set (tx_flags, tfImmediateOrCancel))
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

    // The original in and out quantities
    Amounts m_amount;

    // Amount of input currency remaining.
    Amount m_in_remaining;

    // Amount of output currency we have received.
    Amount m_out_total;

    // Amount of currency that actually flowed.
    Amounts m_flow;

private:
    /** Calculate a flow based on fees and balances.
        @param quality The original quality of amount
    */
    Amounts
    flow (
        Amounts amount,
        Quality const& quality,
        Account const& owner) const
    {
        // Limit taker's input by available funds less fees
        Amount const taker_funds (view ().accountFunds (m_account, amount.in));

        // Get fee rate paid by taker
        std::uint32_t const taker_charge_rate (view ().rippleTransferRate (
            m_account, owner, amount.in.getIssuer()));

        // Skip some math when there's no fee
        if (taker_charge_rate == QUALITY_ONE)
        {
            amount = quality.ceil_in (amount, taker_funds);
        }
        else
        {
            Amount const taker_charge (Amount::saFromRate (taker_charge_rate));
            amount = quality.ceil_in (amount,
                Amount::divide (taker_funds, taker_charge));
        }

        // Best flow the owner can get.
        // Start out assuming entire offer will flow.
        Amounts owner_amount (amount);

        // Limit owner's output by available funds less fees

        // VFALCO TODO Rename accountFounds to make it clear that
        //             it can return a clamped value.
        Amount const owner_funds (view ().accountFunds (owner, owner_amount.out));

        // Get fee rate paid by owner
        std::uint32_t const owner_charge_rate (view ().rippleTransferRate (
            owner, m_account, amount.out.getIssuer()));

        if (owner_charge_rate == QUALITY_ONE)
        {
            // Skip some math when there's no fee
            owner_amount = quality.ceil_out (owner_amount, owner_funds);
        }
        else
        {
            Amount const owner_charge (Amount::saFromRate (owner_charge_rate));
            owner_amount = quality.ceil_out (owner_amount,
                Amount::divide (owner_funds, owner_charge));
        }

        // Calculate the amount that will flow through the offer
        // This does not include the fees.
        return (owner_amount.in < amount.in)
            ? owner_amount
            : amount;
    }

    /** Fill an offer based on the flow amount. */
    TER
    fill (Offer const& offer, Amounts const& amount)
    {
        TER result (tesSUCCESS);
        
        Amounts const remain (
            offer.entry ()->getFieldAmount (sfTakerPays) - amount.in,
            offer.entry ()->getFieldAmount (sfTakerGets) - amount.out);

        offer.entry ()->setFieldAmount (sfTakerPays, remain.in);
        offer.entry ()->setFieldAmount (sfTakerGets, remain.out);
        view ().entryModify (offer.entry());

        // Pay the taker, then the owner
        result = view ().accountSend (offer.account(), account(), amount.out);

        if (result == tesSUCCESS)
            result = view ().accountSend (account(), offer.account(), amount.in);

        return result;
    }

public:
    Taker (LedgerView& view, Account const& account,
        Amounts const& amount, Options const& options)
        : m_view (view)
        , m_account (account)
        , m_options (options)
        , m_quality (amount)
        , m_threshold (m_quality)
        , m_amount (amount)
        , m_in_remaining (amount.in)
        , m_out_total (amount.out.getCurrency(), amount.out.getIssuer())
    {
        // If this is a passive order (tfPassive), this prevents
        // offers at the same quality level from being consumed.
        if (m_options.passive)
            ++m_threshold;

        m_flow.in.clear (amount.in);
        m_flow.out.clear (amount.out);
    }

    LedgerView&
    view() const noexcept
    {
        return m_view;
    }

    /** Returns the amount that flowed through. */
    Amounts const&
    total_flow () const noexcept
    {
        return m_flow;
    }

    /** Returns the account identifier of the taker. */
    Account const&
    account() const noexcept
    {
        return m_account;
    }

    /** Returns `true` if order crossing should not continue.
        Order processing is stopped if the taker's order quantities have
        been reached, or if the taker has run out of input funds.
    */
    bool
    done() const noexcept
    {
        if (m_options.sell)
        {
            // With the sell option, we are finished when
            // we have consumed all the input currency.
            if (m_in_remaining <= zero)
                return true;
        }
        else if (m_out_total >= m_amount.out)
        {
            // With the buy option (!sell) we are finished when we
            // have received the desired amount of output currency.
            return true;
        }

        // We are finished if the taker is out of funds
        return view().accountFunds (account(), m_in_remaining) <= zero;
    }

    /** Returns `true` if the quality does not meet the taker's requirements. */
    bool
    reject (Quality const& quality) const noexcept
    {
        return quality < m_threshold;
    }

    /** Perform direct and bridged offer crossings.
        @return The amounts which flowed through.
    */
    /** @{ */
    TER
    cross (Offer const& offer)
    {
        assert (!done ());
        Amounts limit (offer.amount());
        if (m_options.sell)
        {
            limit = offer.quality().ceil_in (limit, m_in_remaining);
        }
        else
        {
            assert (m_out_total < offer.amount ().out);
            limit = offer.quality ().ceil_out (
                limit, m_amount.out - m_out_total);
            assert (limit.in > zero);
        }
        assert (limit.out <= offer.amount().out);
        assert (limit.in <= offer.amount().in);
        Amounts const amount (flow (limit,
            offer.quality (), offer.account ()));
        m_out_total += amount.out;
        m_in_remaining -= amount.in;
        assert (m_in_remaining >= zero);
        m_flow.in += amount.in;
        m_flow.out += amount.out;
        return fill (offer, amount);
    }

    TER
    cross (Offer const& leg1, Offer const& leg2)
    {
        return tesSUCCESS;
    }
    /** @} */
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
