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

#ifndef RIPPLE_CORE_TYPES_H_INCLUDED
#define RIPPLE_CORE_TYPES_H_INCLUDED

#include <ripple/module/app/ledger/LedgerEntrySet.h>
#include <ripple/types/api/RippleAssets.h>
#include <ripple/types/api/base_uint.h>

#include <chrono>
#include <cstdint>

namespace ripple {
namespace core {

namespace detail {

class AccountTag {};
class CurrencyTag {};

} // detail

typedef base_uint<160, detail::AccountTag> Account;
typedef base_uint<160, detail::CurrencyTag> Currency;

inline std::string to_string(Currency const& c)
{
    return STAmount::createHumanCurrency(c);
}

inline std::string to_string(Account const& a)
{
    return RippleAddress::createHumanAccountID(a);
}

inline std::ostream& operator<< (std::ostream& os, Account const& x)
{
    os << to_string (x);
    return os;
}

inline std::ostream& operator<< (std::ostream& os, Currency const& x)
{
    os << to_string (x);
    return os;
}

/** A mutable view that overlays an immutable ledger to track changes. */
typedef LedgerEntrySet LedgerView;

/** Asset identifiers. */
typedef RippleAsset Asset;
typedef RippleAssetRef AssetRef;

/** Uniquely identifies an order book. */
typedef RippleBook Book;
typedef RippleBookRef BookRef;

/** A clock representing network time.
    This measures seconds since the Ripple epoch as seen
    by the ledger close clock.
*/
class Clock // : public abstract_clock <std::chrono::seconds>
{
public:
    typedef std::uint32_t time_point;
    typedef std::chrono::seconds duration;
};

} // core

inline bool isXRP(core::Currency const& c)
{
    return c == zero;
}

} // ripple

#endif
