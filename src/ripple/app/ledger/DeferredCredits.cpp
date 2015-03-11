//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2012-2015 Ripple Labs Inc.

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

#include <ripple/app/ledger/DeferredCredits.h>
#include <ripple/basics/Log.h>

namespace ripple {
template <class TMap>
void maybeLogCredit (Account const& sender,
                     Account const& receiver,
                     STAmount const& amount,
                     TMap const& adjMap)
{
    using std::get;

    if (!ShouldLog (lsTRACE, DeferredCredits))
        return;

    // write the balances to the log
    std::stringstream str;
    str << "assetXfer: " << sender << ", " << receiver << ", " << amount;
    if (!adjMap.empty ())
    {
        str << " : ";
    }
    for (auto i = adjMap.begin (), e = adjMap.end ();
         i != e; ++i)
    {
        if (i != adjMap.begin ())
        {
            str << ", ";
        }
        auto const& k(i->first);
        auto const& v(i->second);
        str << to_string (get<0> (k)) << " | " <<
            to_string (get<1> (k)) << " | " <<
            get<1> (v).getFullText () << " | " <<
            get<0> (v).getFullText ();
    }
    WriteLog (lsTRACE, DeferredCredits) << str.str ();
}

void DeferredCredits::credit (Account const& sender,
                              Account const& receiver,
                              STAmount const& amount)
{
    using std::get;

    WriteLog (lsTRACE, DeferredCredits)
            << "credit: " << sender << ", " << receiver << ", " << amount;

    assert (sender != receiver);
    assert (!amount.negative ());

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
    maybeLogCredit (sender, receiver, amount, map_);
}

// Get the adjusted balance of main for the
// balance between main and other.
STAmount DeferredCredits::adjustedBalance (Account const& main,
                                           Account const& other,
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

    WriteLog (lsTRACE, DeferredCredits)
            << "adjustedBalance: " << main << ", " <<
            other << ", " << curBalance << ", " << result;

    return result;
}

void DeferredCredits::clear ()
{
    map_.clear ();
}
}
