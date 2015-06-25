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

#ifndef RIPPLE_LEDGER_DEFERREDCREDITS_H_INCLUDED
#define RIPPLE_LEDGER_DEFERREDCREDITS_H_INCLUDED

#include <ripple/protocol/UintTypes.h>
#include <ripple/protocol/STAmount.h>
#include <map>
#include <tuple>

namespace ripple {

class DeferredCredits
{
private:
    // lowAccount, highAccount
    using Key = std::tuple<
        AccountID, AccountID, Currency>;

    // lowAccountCredits, highAccountCredits
    using Value = std::tuple<
        STAmount, STAmount>;

    static
    Key
    makeKey (AccountID const& a1,
        AccountID const& a2,
            Currency const& c);

    std::map<Key, Value> map_;

public:
    // Get the adjusted balance of main for the
    // balance between main and other.
    STAmount
    adjustedBalance (AccountID const& main,
        AccountID const& other,
            STAmount const& curBalance) const;

    void credit (AccountID const& sender,
                 AccountID const& receiver,
                 STAmount const& amount);

    void apply (DeferredCredits& to);

    // VFALCO Is this needed?
    // DEPRECATED
    void clear ();
};

} // ripple

#endif
