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
    Book m_book;
    Account m_account;
    Options m_options;
    Quality m_quality;
    Quality m_threshold;

    // The original in and out quantities
    Amounts m_amount;

    // Amount of input currency remaining.
    Amount m_in;

    // Amount of output currency we have received.
    Amount m_out;

    // Returns the balance of the taker's input currency,
    Amount
    funds() const
    {
        return view().accountFunds (account(), m_in);
    }

public:
    Taker (LedgerView& view, BookRef const& book,
        Account const& account, Amounts const& amount,
            Options const& options)
        : m_view (view)
        , m_book (book)
        , m_account (account)
        , m_options (options)
        , m_quality (amount)
        , m_threshold (m_quality)
        , m_amount (amount)
        , m_in (amount.in)
        , m_out (amount.out.getCurrency(), amount.out.getIssuer())
    {
        // If this is a passive order (tfPassive), this prevents
        // offers at the same quality level from being consumed.
        if (m_options.passive)
            ++m_threshold;
    }

    LedgerView&
    view() const noexcept
    {
        return m_view;
    }

    /** Returns the input and output asset pair identifier. */
    Book const&
    book() const noexcept
    {
        return m_book;
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
            if (m_in <= zero)
                return true;
        }
        else if (m_out >= m_amount.out)
        {
            // With the buy option (!sell) we are finished when we
            // have received the desired amount of output currency.
            return true;
        }

        // We are finished if the taker is out of funds
        return funds() <= zero;
    }

    Quality
    threshold() const noexcept
    {
        return m_threshold;
    }

    /** Returns `true` if the quality does not meet the taker's requirements. */
    bool
    reject (Quality const& quality) const noexcept
    {
        return quality < m_threshold;
    }

    /** Calcualtes the result of applying the taker's funds to the offer.
        @return The flow and flag indicating if the order was consumed.
    */
    std::pair <Amounts, bool>
    fill (Offer const& offer) const
    {
        // Best flow the owner can get.
        // Start out assuming entire offer will flow.
        Amounts owner_amount (offer.amount());

        // Limit owner's output by available funds less fees

        // VFALCO TODO Rename accountFounds to make it clear that
        //             it can return a clamped value.
        Amount const owner_funds (view().accountFunds (
            offer.account(), owner_amount.out));

        // Get fee rate paid by owner
        std::uint32_t const owner_charge_rate (view().rippleTransferRate (
            offer.account(), account(), offer.entry()->getFieldAmount (
                sfTakerGets).getIssuer()));
        Amount const owner_charge (Amount::saFromRate (owner_charge_rate));

        // VFALCO Make Amount::divide skip math if v2 == QUALITY_ONE
        if (owner_charge_rate == QUALITY_ONE)
        {
            // Skip some math when there's no fee
            owner_amount = offer.quality().ceil_out (
                owner_amount, owner_funds);
        }
        else
        {
            owner_amount = offer.quality().ceil_out (owner_amount,
                Amount::divide (owner_funds, owner_charge));
        }

        // Best flow the taker can get.
        // Start out assuming entire offer will flow.
        Amounts taker_amount (offer.amount());

        // Limit taker's input by available funds less fees

        Amount const taker_funds (view().accountFunds (account(), m_in));

        // Get fee rate paid by taker
        std::uint32_t const taker_charge_rate (view().rippleTransferRate (
            account(), offer.account(), offer.entry()->getFieldAmount (
                sfTakerPays).getIssuer()));
        Amount const taker_charge (Amount::saFromRate (taker_charge_rate));

        // VFALCO Make Amount::divide skip math if v2 == QUALITY_ONE
        if (taker_charge_rate == QUALITY_ONE)
        {
            // Skip some math when there's no fee
            taker_amount = offer.quality().ceil_in (
                taker_amount, taker_funds);
        }
        else
        {
            taker_amount = offer.quality().ceil_in (taker_amount,
                Amount::divide (taker_funds, taker_charge));
        }

        // Limit taker's input by options
        if (! m_options.sell)
        {
            assert (m_out < m_amount.out);
            taker_amount = offer.quality().ceil_out (
                taker_amount, m_amount.out - m_out);
            assert (taker_amount.in != zero);
        }

        // Calculate the amount that will flow through the offer
        // This does not include the fees.
        Amounts const flow ((owner_amount.in < taker_amount.in) ?
            owner_amount : taker_amount);

        bool const consumed (flow.out >= owner_amount.out);

        return std::make_pair (flow, consumed);
    }

    /** Process the result of fee and funds calculation on the offer.    
        
        To protect the ledger, conditions which should never occur are
        checked. If the invariants are broken, the processing fails.

        If processing succeeds, the funds are distributed to the taker,
        owner, and issuers.

        @return `false` if processing failed (due to math errors).
    */
    TER
    process (Amounts const& flow, Offer const& offer)
    {
        TER result (tesSUCCESS);

        // VFALCO For the case of !sell, is it possible for the taker
        //        to get a tiny bit more than he asked for?
        // DAVIDS Can you verify?
        assert (m_options.sell || flow.out <= m_amount.out);

        // Calculate remaining portion of offer
        Amounts const remain (
            offer.entry()->getFieldAmount (sfTakerPays) - flow.in,
            offer.entry()->getFieldAmount (sfTakerGets) - flow.out);

        offer.entry()->setFieldAmount (sfTakerPays, remain.in);
        offer.entry()->setFieldAmount (sfTakerGets, remain.out);
        view().entryModify (offer.entry());

        // Pay the taker
        result = view().accountSend (offer.account(), account(), flow.out);

        if (result == tesSUCCESS)
        {
            m_out += flow.out;

            // Pay the owner
            result = view().accountSend (account(), offer.account(), flow.in);

            if (result == tesSUCCESS)
            {
                m_in -= flow.in;
            }
        }

        return result;
    }
};


inline
std::ostream&
operator<< (std::ostream& os, Taker const& taker)
{
    return os << taker.account();
}

}
}

/*

// This code calculates the fees but then we discovered
// that LedgerEntrySet::accountSend does it for you.

Amounts fees;

// Calculate taker fee
if (taker_charge_rate == QUALITY_ONE)
{
    // No fee, skip math
    fees.in = Amount (flow.in.getCurrency(),
        flow.in.getIssuer());
}
else
{
    // VFALCO TODO Check the units (versus 4-arg version of mulRound)
    Amount const in_plus_fees (Amount::mulRound (
        flow.in, taker_charge, true));
    // Make sure the taker has enough to pay the fee
    if (in_plus_fees > taker_funds)
    {
        // Not enough funds, stiff the issuer
        fees.in = taker_funds - flow.in;
    }
    else
    {
        fees.in = in_plus_fees - flow.in;
    }
}

// Calculate owner fee
if (owner_charge_rate == QUALITY_ONE)
{
    // No fee, skip math
    fees.out = Amount (flow.out.getCurrency(),
        flow.out.getIssuer());
}
else
{
    Amount const out_plus_fees (Amount::mulRound (
        flow.out, owner_charge, true));
    // Make sure the owner has enough to pay the fee
    if (out_plus_fees > owner_funds)
    {
        // Not enough funds, stiff the issuer
        fees.out = owner_funds - flow.out;
    }
    else
    {
        fees.out = out_plus_fees - flow.out;
    }
}
*/

#endif
