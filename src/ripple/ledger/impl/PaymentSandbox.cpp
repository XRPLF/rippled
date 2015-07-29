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
#include <cassert>

namespace ripple {

namespace detail {

auto
DeferredCredits::makeKey (AccountID const& a1,
    AccountID const& a2, Currency const& c) ->
        Key
{
    if (a1 < a2)
        return std::make_tuple(a1, a2, c);
    else
        return std::make_tuple(a2, a1, c);
}

void DeferredCredits::credit (AccountID const& sender,
                              AccountID const& receiver,
                              STAmount const& amount)
{
    using std::get;

    assert (sender != receiver);
    assert (!amount.negative());

    auto const k = makeKey (sender, receiver, amount.getCurrency ());
    auto i = map_.find (k);
    if (i == map_.end ())
    {
        Value v;

        if (sender < receiver)
        {
            get<1> (v) = amount;
            get<0> (v) = amount.zeroed ();
        }
        else
        {
            get<1> (v) = amount.zeroed ();
            get<0> (v) = amount;
        }

        map_[k] = v;
    }
    else
    {
        auto& v = i->second;
        if (sender < receiver)
            get<1> (v) += amount;
        else
            get<0> (v) += amount;
    }
}

// Get the adjusted balance of main for the
// balance between main and other.
STAmount DeferredCredits::adjustedBalance (AccountID const& main,
                                           AccountID const& other,
                                           STAmount const& curBalance) const
{
    using std::get;
    STAmount result (curBalance);

    Key const k = makeKey (main, other, curBalance.getCurrency ());
    auto i = map_.find (k);
    if (i != map_.end ())
    {
        auto const& v = i->second;
        if (main < other)
        {
            result -= get<0> (v);
        }
        else
        {
            result -= get<1> (v);
        }
    }

    return result;
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
            using std::get;
            get<0>(r.first->second) += get<0>(p.second);
            get<1>(r.first->second) += get<1>(p.second);
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
    if (ps_)
        return tab_.adjustedBalance (
            account, issuer, ps_->balanceHook (account, issuer, amount));
    return tab_.adjustedBalance(
        account, issuer, amount);
}

void
PaymentSandbox::creditHook (AccountID const& from,
    AccountID const& to,
        STAmount const& amount)
{
    tab_.credit(from, to, amount);
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
