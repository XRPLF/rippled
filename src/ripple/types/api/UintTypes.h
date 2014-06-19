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

#ifndef RIPPLE_TYPES_BASICS
#define RIPPLE_TYPES_BASICS

#include <unordered_set>

#include <ripple/types/api/base_uint.h>

namespace ripple {
namespace detail {

class AccountTag {};
class CurrencyTag {};
class DirectoryTag {};

} // detail

/** Directory is an index into the directory of offer books.
    The last 64 bits of this are the quality. */
typedef base_uint<256, detail::DirectoryTag> Directory;

/** Account is a hash representing a specific account. */
typedef base_uint<160, detail::AccountTag> Account;

/** Currency is a hash representing a specific currency. */
typedef base_uint<160, detail::CurrencyTag> Currency;

typedef std::unordered_set<uint160> CurrencySet;

/** A special account that's used as the "issuer" for XRP. */
Account const& xrpIssuer();

/** XRP currency. */
Currency const& xrpCurrency();

/** A placeholder for empty accounts. */
Account const& noAccount();

/** A placeholder for empty currencies. */
Currency const& noCurrency();

/** We deliberately disallow the currency that looks like "XRP" because too
    many people were using it instead of the correct XRP currency. */
Currency const& badCurrency();

// TODO(tom): this will go when we get rid of legacy types.
inline bool isXRP(uint160 const& c)
{
    return c == zero;
}

inline bool isXRP(Currency const& c)
{
    return c == zero;
}

inline bool isXRP(Account const& c)
{
    return c == zero;
}

std::string to_string(Account const&);
std::string to_string(Currency const& c);

/** Tries to convert a string to a currency, returns true on success. */
bool to_currency(Currency&, std::string const&);

/** Tries to convert a string to a currency, returns noCurrency() on failure. */
Currency to_currency(std::string const&);

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

} // ripple

#endif
