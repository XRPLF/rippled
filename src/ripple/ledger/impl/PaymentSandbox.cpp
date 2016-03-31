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

#include <BeastConfig.h>
#include <ripple/ledger/PaymentSandbox.h>
#include <ripple/ledger/View.h>

#include <boost/optional.hpp>

#include <cassert>

namespace ripple {

namespace detail {

auto DeferredCredits::makeKey (AccountID const& a1,
    AccountID const& a2,
    Currency const& c) -> Key
{
    if (a1 < a2)
        return std::make_tuple(a1, a2, c);
    else
        return std::make_tuple(a2, a1, c);
}

void
DeferredCredits::credit (AccountID const& sender,
    AccountID const& receiver,
    STAmount const& amount,
    STAmount const& preCreditBalanceSender)
{
    assert (sender != receiver);
    assert (!amount.negative());

    auto const k = makeKey (sender, receiver, amount.getCurrency ());
    auto i = map_.find (k);
    if (i == map_.end ())
    {
        Value v;

        if (sender < receiver)
        {
            v.highAccCredits = amount;
            v.lowAccCredits = amount.zeroed ();
            v.lowAccOrgBalance = preCreditBalanceSender;
        }
        else
        {
            v.highAccCredits = amount.zeroed ();
            v.lowAccCredits = amount;
            v.lowAccOrgBalance = -preCreditBalanceSender;
        }

        map_[k] = v;
    }
    else
    {
        // only record the balance the first time, do not record it here
        auto& v = i->second;
        if (sender < receiver)
            v.highAccCredits += amount;
        else
            v.lowAccCredits += amount;
    }
}

// Get the adjustments for the balance between main and other.
auto
DeferredCredits::adjustments (AccountID const& main,
    AccountID const& other,
    Currency const& currency) const -> boost::optional<adjustment>
{
    boost::optional<adjustment> result;

    Key const k = makeKey (main, other, currency);
    auto i = map_.find (k);
    if (i == map_.end ())
        return result;

    auto const& v = i->second;

    if (main < other)
    {
        result.emplace (v.highAccCredits, v.lowAccCredits, v.lowAccOrgBalance);
        return result;
    }
    else
    {
        result.emplace (v.lowAccCredits, v.highAccCredits, -v.lowAccOrgBalance);
        return result;
    }
}

void DeferredCredits::apply(
    DeferredCredits& to)
{
    for (auto& p : map_)
    {
        auto r =
            to.map_.emplace(p);
        if (! r.second)
        {
            auto& toVal = r.first->second;
            auto const& fromVal = p.second;
            toVal.lowAccCredits += fromVal.lowAccCredits;
            toVal.highAccCredits += fromVal.highAccCredits;
            // Do not update the org balance, it's already correct
        }
    }
}

void DeferredCredits::clear ()
{
    map_.clear ();
}

} // detail

STAmount
PaymentSandbox::balanceHook (AccountID const& account,
    AccountID const& issuer,
        STAmount const& amount) const
{
    // There are two algorithms here. The first takes the current amount and
    // subtracts the recorded credits. The second algorithm remembers the origional
    // balance, and subtracts the debits. The second algorithm should be more numerically
    // stable than the first. Consider a large credit with a small initial balance.
    // The first algorithm computes (B+C)-C (where B+C will the the amount passed in).
    // The second algorithm return B. When B and C differ by large magnitudes, (B+C)-C may
    // not equal B.

    auto const currency = amount.getCurrency ();
    auto const switchover = flowV2Switchover (info ().parentCloseTime);

    auto adjustedAmt = amount;
    if (switchover)
    {
        auto delta = amount.zeroed ();
        auto lastBal = amount;
        for (auto curSB = this; curSB; curSB = curSB->ps_)
        {
            if (auto adj = curSB->tab_.adjustments (account, issuer, currency))
            {
                delta += adj->debits;
                lastBal = adj->orgBalance;
            }
        }
        adjustedAmt = std::min(amount, lastBal - delta);
    }
    else
    {
        for (auto curSB = this; curSB; curSB = curSB->ps_)
        {
            if (auto adj = curSB->tab_.adjustments (account, issuer, currency))
            {
                adjustedAmt -= adj->credits;
            }
        }
    }

    assert (!isXRP(issuer) || adjustedAmt >= beast::zero);
    return adjustedAmt;
}

void
PaymentSandbox::creditHook (AccountID const& from,
    AccountID const& to,
        STAmount const& amount,
            STAmount const& preCreditBalance)
{
    tab_.credit (from, to, amount, preCreditBalance);
}

void
PaymentSandbox::apply (RawView& to)
{
    assert(! ps_);
    items_.apply(to);
}

void
PaymentSandbox::apply (PaymentSandbox& to)
{
    assert(ps_ == &to);
    items_.apply(to);
    tab_.apply(to.tab_);
}

}  // ripple
