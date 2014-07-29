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

#include <ripple/module/app/book/Taker.h>

namespace ripple {
namespace core {

Taker::Taker (LedgerView& view, Account const& account,
    Amounts const& amount, Options const& options)
    : m_view (view)
    , m_account (account)
    , m_options (options)
    , m_quality (amount)
    , m_threshold (m_quality)
    , m_amount (amount)
    , m_remain (amount)
{
    assert (m_remain.in > zero);
    assert (m_remain.out > zero);

    // If this is a passive order (tfPassive), this prevents
    // offers at the same quality level from being consumed.
    if (m_options.passive)
        ++m_threshold;
}

Amounts
Taker::remaining_offer () const
{
    // If the taker is done, then there's no offer to place.
    if (done ())
        return Amounts (m_amount.in.zeroed(), m_amount.out.zeroed());

    // Avoid math altogether if we didn't cross.
   if (m_amount == m_remain)
       return m_amount;

    if (m_options.sell)
    {
        assert (m_remain.in > zero);

        // We scale the output based on the remaining input:
        return Amounts (m_remain.in, Amount::divRound (
            m_remain.in, m_quality.rate (), m_remain.out, true));
    }

    assert (m_remain.out > zero);

    // We scale the input based on the remaining output:
    return Amounts (Amount::mulRound (
        m_remain.out, m_quality.rate (), m_remain.in, true), m_remain.out);
}

/** Calculate the amount particular user could get through an offer.
    @param amount the maximum flow that is available to the taker.
    @param offer the offer to flow through.
    @param taker the person taking the offer.
    @return the maximum amount that can flow through this offer.
*/
Amounts
Taker::flow (Amounts amount, Offer const& offer, Account const& taker)
{
    // Limit taker's input by available funds less fees
    Amount const taker_funds (view ().accountFunds (
        taker, amount.in, fhZERO_IF_FROZEN));

    // Get fee rate paid by taker
    std::uint32_t const taker_charge_rate (view ().rippleTransferRate (
        taker, offer.account (), amount.in.getIssuer()));

    // Skip some math when there's no fee
    if (taker_charge_rate == QUALITY_ONE)
    {
        amount = offer.quality ().ceil_in (amount, taker_funds);
    }
    else
    {
        Amount const taker_charge (Amount::saFromRate (taker_charge_rate));
        amount = offer.quality ().ceil_in (amount,
            Amount::divide (taker_funds, taker_charge));
    }

    // Best flow the owner can get.
    // Start out assuming entire offer will flow.
    Amounts owner_amount (amount);

    // Limit owner's output by available funds less fees
    Amount const owner_funds (view ().accountFunds (
        offer.account (), owner_amount.out, fhZERO_IF_FROZEN));

    // Get fee rate paid by owner
    std::uint32_t const owner_charge_rate (view ().rippleTransferRate (
        offer.account (), taker, amount.out.getIssuer()));

    if (owner_charge_rate == QUALITY_ONE)
    {
        // Skip some math when there's no fee
        owner_amount = offer.quality ().ceil_out (owner_amount, owner_funds);
    }
    else
    {
        Amount const owner_charge (Amount::saFromRate (owner_charge_rate));
        owner_amount = offer.quality ().ceil_out (owner_amount,
            Amount::divide (owner_funds, owner_charge));
    }

    // Calculate the amount that will flow through the offer
    // This does not include the fees.
    return (owner_amount.in < amount.in)
        ? owner_amount
        : amount;
}

// Adjust an offer to indicate that we are consuming some (or all) of it.
void
Taker::consume (Offer const& offer, Amounts const& consumed) const
{
    Amounts const& remaining (offer.amount ());

    assert (remaining.in > zero && remaining.out > zero);
    assert (remaining.in >= consumed.in && remaining.out >= consumed.out);

    offer.entry ()->setFieldAmount (sfTakerPays, remaining.in - consumed.in);
    offer.entry ()->setFieldAmount (sfTakerGets, remaining.out - consumed.out);

    view ().entryModify (offer.entry());

    assert (offer.entry ()->getFieldAmount (sfTakerPays) >= zero);
    assert (offer.entry ()->getFieldAmount (sfTakerGets) >= zero);
}

// Fill a direct offer.
//   @param offer the offer we are going to use.
//   @param amount the amount to flow through the offer.
//   @returns: tesSUCCESS if successful, or an error code otherwise.
TER
Taker::fill (Offer const& offer, Amounts const& amount)
{
    consume (offer, amount);

    // Pay the taker, then the owner
    TER result = view ().accountSend (offer.account(), account(), amount.out);

    if (result == tesSUCCESS)
        result = view ().accountSend (account(), offer.account(), amount.in);

    return result;
}

// Fill a bridged offer.
//   @param leg1 the first leg we are going to use.
//   @param amount1 the amount to flow through the first leg of the offer.
//   @param leg2 the second leg we are going to use.
//   @param amount2 the amount to flow through the second leg of the offer.
//   @return tesSUCCESS if successful, or an error code otherwise.
TER
Taker::fill (
    Offer const& leg1, Amounts const& amount1,
    Offer const& leg2, Amounts const& amount2)
{
    assert (amount1.out == amount2.in);

    consume (leg1, amount1);
    consume (leg2, amount2);

    /* It is possible that m_account is the same as leg1.account, leg2.account
     * or both. This could happen when bridging over one's own offer. In that
     * case, accountSend won't actually do a send, which is what we want.
     */
    TER result = view ().accountSend (m_account, leg1.account (), amount1.in);

    if (result == tesSUCCESS)
        result = view ().accountSend (leg1.account (), leg2.account (), amount1.out);

    if (result == tesSUCCESS)
        result = view ().accountSend (leg2.account (), m_account, amount2.out);

    return result;
}

bool
Taker::done () const
{
    if (m_options.sell && (m_remain.in <= zero))
    {
        // Sell semantics: we consumed all the input currency
        return true;
    }

    if (!m_options.sell && (m_remain.out <= zero))
    {
        // Buy semantics: we received the desired amount of output currency
        return true;
    }

    // We are finished if the taker is out of funds
    return view().accountFunds (
        account(), m_remain.in, fhZERO_IF_FROZEN) <= zero;
}

TER
Taker::cross (Offer const& offer)
{
    assert (!done ());

    /* Before we call flow we must set the limit right; for buy semantics we
       need to clamp the output. And we always want to clamp the input.
     */
    Amounts limit (offer.amount());

    if (! m_options.sell)
        limit = offer.quality ().ceil_out (limit, m_remain.out);
    limit = offer.quality().ceil_in (limit, m_remain.in);

    assert (limit.in <= offer.amount().in);
    assert (limit.out <= offer.amount().out);
    assert (limit.in <= m_remain.in);

    Amounts const amount (flow (limit, offer, account ()));

    m_remain.out -= amount.out;
    m_remain.in -= amount.in;

    assert (m_remain.in >= zero);
    return fill (offer, amount);
}

TER
Taker::cross (Offer const& leg1, Offer const& leg2)
{
    assert (!done ());

    assert (leg1.amount ().out.isNative ());
    assert (leg2.amount ().in.isNative ());

    Amounts amount1 (leg1.amount());
    Amounts amount2 (leg2.amount());

    if (m_options.sell)
        amount1 = leg1.quality().ceil_in (amount1, m_remain.in);
    else
        amount2 = leg2.quality().ceil_out (amount2, m_remain.out);

    if (amount1.out <= amount2.in)
        amount2 = leg2.quality().ceil_in (amount2, amount1.out);
    else
        amount1 = leg1.quality().ceil_out (amount1, amount2.in);

    assert (amount1.out == amount2.in);

    // As written, flow can't handle a 3-party transfer, but this works for
    // us because the output of leg1 and the input leg2 are XRP.
    Amounts flow1 (flow (amount1, leg1, m_account));

    amount2 = leg2.quality().ceil_in (amount2, flow1.out);

    Amounts flow2 (flow (amount2, leg2, m_account));

    m_remain.out -= amount2.out;
    m_remain.in -= amount1.in;

    return fill (leg1, flow1, leg2, flow2);
}

}
}
