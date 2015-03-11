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

#ifndef RIPPLE_APP_LEDGER_CACHEDCREDITS_H_INCLUDED
#define RIPPLE_APP_LEDGER_CACHEDCREDITS_H_INCLUDED

#include <ripple/protocol/UintTypes.h>
#include <ripple/protocol/STAmount.h>
#include <map>
#include <tuple>

namespace ripple {
class DeferredCredits
{
private:
    // lowAccount, highAccount
    using Key = std::tuple<Account, Account, Currency>;
    // lowAccountCredits, highAccountCredits
    using Value = std::tuple<STAmount, STAmount>;
    static inline
    Key makeKey(Account const& a1, Account const& a2, Currency const& c)
    {
        if (a1 < a2)
            return std::make_tuple(a1, a2, c);
        else
            return std::make_tuple(a2, a1, c);
    }

    std::map<Key, Value> map_;

public:
    void credit (Account const& sender,
                 Account const& receiver,
                 STAmount const& amount);
    // Get the adjusted balance of main for the
    // balance between main and other.
    STAmount adjustedBalance (Account const& main,
                              Account const& other,
                              STAmount const& curBalance) const;
    void clear ();
};
}
#endif
