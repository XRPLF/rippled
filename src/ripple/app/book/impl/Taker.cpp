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

#include <ripple/app/book/Taker.h>

namespace ripple {
namespace core {

Amount
BasicTaker::Rate::divide (Amount const& amount) const
{
    if (quality_ == QUALITY_ONE)
        return amount;

    return ripple::divide (amount, rate_, amount.issue ());
}

Amount
BasicTaker::Rate::multiply (Amount const& amount) const
{
    if (quality_ == QUALITY_ONE)
        return amount;

    return ripple::multiply (amount, rate_, amount.issue ());
}

BasicTaker::BasicTaker (
        CrossType cross_type, Account const& account, Amounts const& amount,
        Quality const& quality, std::uint32_t flags, std::uint32_t rate_in,
        std::uint32_t rate_out)
    : account_ (account)
    , quality_ (quality)
    , threshold_ (quality_)
    , sell_ (flags & tfSell)
    , original_ (amount)
    , remaining_ (amount)
    , issue_in_ (remaining_.in.issue ())
    , issue_out_ (remaining_.out.issue ())
    , m_rate_in (rate_in)
    , m_rate_out (rate_out)
    , cross_type_ (cross_type)
{
    assert (remaining_.in > zero);
    assert (remaining_.out > zero);

    assert (m_rate_in != 0);
    assert (m_rate_out != 0);

    // If we are dealing with a particular flavor, make sure that it's the
    // flavor we expect:
    assert (cross_type != CrossType::XrpToIou ||
        (isXRP (issue_in ()) && !isXRP (issue_out ())));

    assert (cross_type != CrossType::IouToXrp ||
        (!isXRP (issue_in ()) && isXRP (issue_out ())));

    // And make sure we're not crossing XRP for XRP
    assert (!isXRP (issue_in ()) || !isXRP (issue_out ()));

    // If this is a passive order, we adjust the quality so as to prevent offers
    // at the same quality level from being consumed.
    if (flags & tfPassive)
        ++threshold_;
}

BasicTaker::Rate
BasicTaker::effective_rate (
    std::uint32_t rate, Issue const &issue,
    Account const& from, Account const& to)
{
    assert (rate != 0);

    if (rate != QUALITY_ONE)
    {
        // We ignore the transfer if the sender is also the recipient since no
        // actual transfer takes place in that case. We also ignore if either
        // the sender or the receiver is the issuer.

        if (from != to && from != issue.account && to != issue.account)
            return Rate (rate);
    }

    return Rate (QUALITY_ONE);
}

Amounts
BasicTaker::remaining_offer () const
{
    // If the taker is done, then there's no offer to place.
    if (done ())
        return Amounts (remaining_.in.zeroed(), remaining_.out.zeroed());

    // Avoid math altogether if we didn't cross.
   if (original_ == remaining_)
       return original_;

    if (sell_)
    {
        assert (remaining_.in > zero);

        // We scale the output based on the remaining input:
        return Amounts (remaining_.in, divRound (
            remaining_.in, quality_.rate (), remaining_.out, true));
    }

    assert (remaining_.out > zero);

    // We scale the input based on the remaining output:
    return Amounts (mulRound (
        remaining_.out, quality_.rate (), remaining_.in, true), remaining_.out);
}

Amounts const&
BasicTaker::original_offer () const
{
    return original_;
}

// TODO: the presence of 'output' is an artifact caused by the fact that
// Amounts carry issue information which should be decoupled.
static
Amount
qual_div (Amount const& amount, Quality const& quality, Amount const& output)
{
    auto result = divide (amount, quality.rate (), output.issue ());
    return std::min (result, output);
}

static
Amount
qual_mul (Amount const& amount, Quality const& quality, Amount const& output)
{
    auto result = multiply (amount, quality.rate (), output.issue ());
    return std::min (result, output);
}

BasicTaker::Flow
BasicTaker::flow_xrp_to_iou (
    Amounts const& order, Quality quality,
    Amount const& owner_funds, Amount const& taker_funds,
    Rate const& rate_out)
{
    Flow f;
    f.order = order;
    f.issuers.out = rate_out.multiply (f.order.out);

    // Clamp on owner balance
    if (owner_funds < f.issuers.out)
    {
        f.issuers.out = owner_funds;
        f.order.out = rate_out.divide (f.issuers.out);
        f.order.in = qual_mul (f.order.out, quality, f.order.in);
    }

    // Clamp if taker wants to limit the output
    if (!sell_ && remaining_.out < f.order.out)
    {
        f.order.out = remaining_.out;
        f.order.in = qual_mul (f.order.out, quality, f.order.in);
        f.issuers.out = rate_out.multiply (f.order.out);
    }

    // Clamp on the taker's funds
    if (taker_funds < f.order.in)
    {
        f.order.in = taker_funds;
        f.order.out = qual_div (f.order.in, quality, f.order.out);
        f.issuers.out = rate_out.multiply (f.order.out);
    }

    // Clamp on remaining offer if we are not handling the second leg
    // of an autobridge.
    if (cross_type_ == CrossType::XrpToIou && (remaining_.in < f.order.in))
    {
        f.order.in = remaining_.in;
        f.order.out = qual_div (f.order.in, quality, f.order.out);
        f.issuers.out = rate_out.multiply (f.order.out);
    }

    return f;
}

BasicTaker::Flow
BasicTaker::flow_iou_to_xrp (
    Amounts const& order, Quality quality,
    Amount const& owner_funds, Amount const& taker_funds,
    Rate const& rate_in)
{
    Flow f;
    f.order = order;
    f.issuers.in = rate_in.multiply (f.order.in);

    // Clamp on owner's funds
    if (owner_funds < f.order.out)
    {
        f.order.out = owner_funds;
        f.order.in = qual_mul (f.order.out, quality, f.order.in);
        f.issuers.in = rate_in.multiply (f.order.in);
    }

    // Clamp if taker wants to limit the output and we are not the
    // first leg of an autobridge.
    if (!sell_ && cross_type_ == CrossType::IouToXrp)
    {
        if (remaining_.out < f.order.out)
        {
            f.order.out = remaining_.out;
            f.order.in = qual_mul (f.order.out, quality, f.order.in);
            f.issuers.in = rate_in.multiply (f.order.in);
        }
    }

    // Clamp on the taker's input offer
    if (remaining_.in < f.order.in)
    {
        f.order.in = remaining_.in;
        f.issuers.in = rate_in.multiply (f.order.in);
        f.order.out = qual_div (f.order.in, quality, f.order.out);
    }

    // Clamp on the taker's input balance
    if (taker_funds < f.issuers.in)
    {
        f.issuers.in = taker_funds;
        f.order.in = rate_in.divide (f.issuers.in);
        f.order.out = qual_div (f.order.in, quality, f.order.out);
    }

    return f;
}

BasicTaker::Flow
BasicTaker::flow_iou_to_iou (
    Amounts const& order, Quality quality,
    Amount const& owner_funds, Amount const& taker_funds,
    Rate const& rate_in, Rate const& rate_out)
{
    Flow f;
    f.order = order;
    f.issuers.in = rate_in.multiply (f.order.in);
    f.issuers.out = rate_out.multiply (f.order.out);

    // Clamp on owner balance
    if (owner_funds < f.issuers.out)
    {
        f.issuers.out = owner_funds;
        f.order.out = rate_out.divide (f.issuers.out);
        f.order.in = qual_mul (f.order.out, quality, f.order.in);
        f.issuers.in = rate_in.multiply (f.order.in);
    }

    // Clamp on taker's offer
    if (!sell_ && remaining_.out < f.order.out)
    {
        f.order.out = remaining_.out;
        f.order.in = qual_mul (f.order.out, quality, f.order.in);
        f.issuers.out = rate_out.multiply (f.order.out);
        f.issuers.in = rate_in.multiply (f.order.in);
    }

    // Clamp on the taker's input balance and input offer
    if (remaining_.in < f.order.in)
    {
        f.order.in = remaining_.in;
        f.issuers.in = rate_in.multiply (f.order.in);
        f.order.out = qual_div (f.order.in, quality, f.order.out);
        f.issuers.out = rate_out.multiply (f.order.out);
    }

    if (taker_funds < f.order.in)
    {
        f.issuers.in = taker_funds;
        f.order.in = rate_in.divide (f.issuers.in);
        f.order.out = qual_div (f.order.in, quality, f.order.out);
        f.issuers.out = rate_out.multiply (f.order.out);
    }

    return f;
}

bool
BasicTaker::done () const
{
    // Sell semantics: we consumed all the input currency
    if (sell_ && (remaining_.in <= zero))
        return true;

    // Buy semantics: we received the desired amount of output currency
    if (!sell_ && (remaining_.out <= zero))
        return true;

    // We are finished if the taker is out of funds
    if (get_funds (account(), remaining_.in) <= zero)
        return true;

    return false;
}

// Calculates the direct flow through the specified offer
BasicTaker::Flow
BasicTaker::do_cross (Amounts offer, Quality quality, Account const& owner)
{
    assert (!done ());

    auto const owner_funds = get_funds (owner, offer.out);
    auto const taker_funds = get_funds (account (), offer.in);

    Flow result;

    if (cross_type_ == CrossType::XrpToIou)
    {
        result = flow_xrp_to_iou (offer, quality, owner_funds, taker_funds, 
            out_rate (owner, account ()));
    }
    else if (cross_type_ == CrossType::IouToXrp)
    {
        result = flow_iou_to_xrp (offer, quality, owner_funds, taker_funds,
            in_rate (owner, account ()));
    }
    else
    {
        result = flow_iou_to_iou (offer, quality, owner_funds, taker_funds,
            in_rate (owner, account ()), out_rate (owner, account ()));
    }

    if (!result.sanity_check ())
        throw std::logic_error ("Computed flow fails sanity check.");

    remaining_.out -= result.order.out;
    remaining_.in -= result.order.in;

    assert (remaining_.in >= zero);

    return result;
}

// Calculates the bridged flow through the specified offers
std::pair<BasicTaker::Flow, BasicTaker::Flow>
BasicTaker::do_cross (
    Amounts offer1, Quality quality1, Account const& owner1,
    Amounts offer2, Quality quality2, Account const& owner2)
{
    assert (!done ());

    assert (!offer1.in.isNative ());
    assert (offer1.out.isNative ());
    assert (offer2.in.isNative ());
    assert (!offer2.out.isNative ());

    // If the taker owns the first leg of the offer, then the taker's available
    // funds aren't the limiting factor for the input - the offer itself is.
    auto leg1_in_funds = get_funds (account (), offer1.in);

    if (account () == owner1)
        leg1_in_funds = std::max (leg1_in_funds, offer1.in);

    // If the taker owns the second leg of the offer, then the taker's available
    // funds are not the limiting factor for the output - the offer itself is.
    auto leg2_out_funds = get_funds (owner2, offer2.out);

    if (account () == owner2)
        leg2_out_funds = std::max (leg2_out_funds, offer2.out);

    // The amount available to flow via XRP is the amount that the owner of the
    // first leg of the bridge has, up to the first leg's output.
    //
    // But, when both legs of a bridge are owned by the same person, the amount
    // of XRP that can flow between the two legs is, essentially, infinite
    // since all the owner is doing is taking out XRP of his left pocket
    // and putting it in his right pocket. In that case, we set the available
    // XRP to the largest of the two offers.
    auto xrp_funds = get_funds (owner1, offer1.out);

    if (owner1 == owner2)
        xrp_funds = std::max (offer1.out, offer2.in);

    auto const leg1_rate = in_rate (owner1, account ());
    auto const leg2_rate = out_rate (owner2, account ());

    // Attempt to determine the maximal flow that can be achieved across each
    // leg independent of the other.
    auto flow1 = flow_iou_to_xrp (offer1, quality1, xrp_funds, leg1_in_funds, leg1_rate);

    if (!flow1.sanity_check ())
        throw std::logic_error ("Computed flow1 fails sanity check.");

    auto flow2 = flow_xrp_to_iou (offer2, quality2, leg2_out_funds, xrp_funds, leg2_rate);

    if (!flow2.sanity_check ())
        throw std::logic_error ("Computed flow2 fails sanity check.");

    // We now have the maximal flows across each leg individually. We need to
    // equalize them, so that the amount of XRP that flows out of the first leg
    // is the same as the amount of XRP that flows into the second leg. We take
    // the side which is the limiting factor (if any) and adjust the other.
    if (flow1.order.out < flow2.order.in)
    {
        // Adjust the second leg of the offer down:
        flow2.order.in = flow1.order.out;
        flow2.order.out = qual_div (flow2.order.in, quality2, flow2.order.out);
        flow2.issuers.out = leg2_rate.multiply (flow2.order.out);
    }
    else if (flow1.order.out > flow2.order.in)
    {
        // Adjust the first leg of the offer down:
        flow1.order.out = flow2.order.in;
        flow1.order.in = qual_mul (flow1.order.out, quality1, flow1.order.in);
        flow1.issuers.in = leg1_rate.multiply (flow1.order.in);
    }

    if (flow1.order.out != flow2.order.in)
        throw std::logic_error ("Bridged flow is out of balance.");

    remaining_.out -= flow2.order.out;
    remaining_.in -= flow1.order.in;

    return std::make_pair (flow1, flow2);
}

//==============================================================================

std::uint32_t
Taker::calculateRate (
    LedgerView& view, Account const& issuer, Account const& account)
{
    return isXRP (issuer) || (account == issuer)
        ? QUALITY_ONE
        : rippleTransferRate (view, issuer);
}

Taker::Taker (CrossType cross_type, LedgerView& view, Account const& account,
        Amounts const& offer, std::uint32_t flags)
    : BasicTaker (cross_type, account, offer, Quality(offer), flags,
        calculateRate(view, offer.in.getIssuer(), account),
        calculateRate(view, offer.out.getIssuer(), account))
    , m_view (view)
    , xrp_flow_ (0)
    , direct_crossings_ (0)
    , bridge_crossings_ (0)
{
    assert (issue_in () == offer.in.issue ());
    assert (issue_out () == offer.out.issue ());
}

void
Taker::consume_offer (Offer const& offer, Amounts const& order)
{
    if (order.in < zero)
        throw std::logic_error ("flow with negative input.");

    if (order.out < zero)
        throw std::logic_error ("flow with negative output.");

    return offer.consume (m_view, order);
}

Amount
Taker::get_funds (Account const& account, Amount const& funds) const
{
    return m_view.accountFunds (account, funds, fhZERO_IF_FROZEN);
}

TER Taker::transfer_xrp (
    Account const& from,
    Account const& to,
    Amount const& amount)
{
    if (!isXRP (amount))
        throw std::logic_error ("Using transfer_xrp with IOU");

    if (from == to)
        return tesSUCCESS;

    return m_view.transfer_xrp (from, to, amount);
}

TER Taker::redeem_iou (
    Account const& account,
    Amount const& amount,
    Issue const& issue)
{
    if (isXRP (amount))
        throw std::logic_error ("Using redeem_iou with XRP");

    if (account == issue.account)
        return tesSUCCESS;

    return m_view.redeem_iou (account, amount, issue);
}

TER Taker::issue_iou (
    Account const& account,
    Amount const& amount,
    Issue const& issue)
{
    if (isXRP (amount))
        throw std::logic_error ("Using issue_iou with XRP");

    if (account == issue.account)
        return tesSUCCESS;

    return m_view.issue_iou (account, amount, issue);
}

// Performs funds transfers to fill the given offer and adjusts offer.
TER
Taker::fill (BasicTaker::Flow const& flow, Offer const& offer)
{
    // adjust offer
    consume_offer (offer, flow.order);

    TER result = tesSUCCESS;

    if (cross_type () != CrossType::XrpToIou)
    {
        assert (!isXRP (flow.order.in));

        if(result == tesSUCCESS)
            result = redeem_iou (account (), flow.issuers.in, flow.issuers.in.issue ());

        if (result == tesSUCCESS)
            result = issue_iou (offer.owner (), flow.order.in, flow.order.in.issue ());
    }
    else
    {
        assert (isXRP (flow.order.in));

        if (result == tesSUCCESS)
            result = transfer_xrp (account (), offer.owner (), flow.order.in);
    }

    // Now send funds from the account whose offer we're taking
    if (cross_type () != CrossType::IouToXrp)
    {
        assert (!isXRP (flow.order.out));

        if(result == tesSUCCESS)
            result = redeem_iou (offer.owner (), flow.issuers.out, flow.issuers.out.issue ());

        if (result == tesSUCCESS)
            result = issue_iou (account (), flow.order.out, flow.order.out.issue ());
    }
    else
    {
        assert (isXRP (flow.order.out));

        if (result == tesSUCCESS)
            result = transfer_xrp (offer.owner (), account (), flow.order.out);
    }

    if (result == tesSUCCESS)
        direct_crossings_++;

    return result;
}

// Performs bridged funds transfers to fill the given offers and adjusts offers.
TER
Taker::fill (
    BasicTaker::Flow const& flow1, Offer const& leg1,
    BasicTaker::Flow const& flow2, Offer const& leg2)
{
    // Adjust offers accordingly
    consume_offer (leg1, flow1.order);
    consume_offer (leg2, flow2.order);

    TER result = tesSUCCESS;

    // Taker to leg1: IOU
    if (leg1.owner () != account ())
    {
        if (result == tesSUCCESS)
            result = redeem_iou (account (), flow1.issuers.in, flow1.issuers.in.issue ());

        if (result == tesSUCCESS)
            result = issue_iou (leg1.owner (), flow1.order.in, flow1.order.in.issue ());
    }

    // leg1 to leg2: bridging over XRP
    if (result == tesSUCCESS)
        result = transfer_xrp (leg1.owner (), leg2.owner (), flow1.order.out);

    // leg2 to Taker: IOU
    if (leg2.owner () != account ())
    {
        if (result == tesSUCCESS)
            result = redeem_iou (leg2.owner (), flow2.issuers.out, flow2.issuers.out.issue ());

        if (result == tesSUCCESS)
            result = issue_iou (account (), flow2.order.out, flow2.order.out.issue ());
    }

    if (result == tesSUCCESS)
    {
        bridge_crossings_++;
        xrp_flow_ += flow1.order.out;
    }

    return result;
}

TER
Taker::cross (Offer const& offer)
{
    // In direct crossings, at least one leg must not be XRP.
    if (isXRP (offer.amount ().in) && isXRP (offer.amount ().out))
        return tefINTERNAL;

    auto const amount = do_cross (
        offer.amount (), offer.quality (), offer.owner ());

    return fill (amount, offer);
}

TER
Taker::cross (Offer const& leg1, Offer const& leg2)
{
    // In bridged crossings, XRP must can't be the input to the first leg
    // or the output of the second leg.
    if (isXRP (leg1.amount ().in) || isXRP (leg2.amount ().out))
        return tefINTERNAL;

    auto ret = do_cross (
        leg1.amount (), leg1.quality (), leg1.owner (),
        leg2.amount (), leg2.quality (), leg2.owner ());

    return fill (ret.first, leg1, ret.second, leg2);
}

}
}
