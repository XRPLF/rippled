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

#ifndef RIPPLE_PROTOCOL_UINTTYPES_H_INCLUDED
#define RIPPLE_PROTOCOL_UINTTYPES_H_INCLUDED

#include <ripple/basics/UnorderedContainers.h>
#include <ripple/basics/base_uint.h>
#include <ripple/beast/utility/Zero.h>
#include <ripple/protocol/AccountID.h>

namespace ripple {
namespace detail {

class CurrencyTag
{
public:
    explicit CurrencyTag() = default;
};

class DirectoryTag
{
public:
    explicit DirectoryTag() = default;
};

class NodeIDTag
{
public:
    explicit NodeIDTag() = default;
};

}  // namespace detail

/** Directory is an index into the directory of offer books.
    The last 64 bits of this are the quality. */
using Directory = base_uint<256, detail::DirectoryTag>;

/** Currency is a hash representing a specific currency. */
using Currency = base_uint<160, detail::CurrencyTag>;

/** NodeID is a 160-bit hash representing one node. */
using NodeID = base_uint<160, detail::NodeIDTag>;

/** XRP currency. */
Currency const&
xrpCurrency();

/** A placeholder for empty currencies. */
Currency const&
noCurrency();

/** We deliberately disallow the currency that looks like "XRP" because too
    many people were using it instead of the correct XRP currency. */
Currency const&
badCurrency();

inline bool
isXRP(Currency const& c)
{
    return c == beast::zero;
}

inline bool
isFakeXRP(Currency const& c)
{
    return c == badCurrency();
}

/** Returns "", "XRP", or three letter ISO code. */
std::string
to_string(Currency const& c);

/** Tries to convert a string to a Currency, returns true on success.

    @note This function will return success if the resulting currency is
          badCurrency(). This legacy behavior is unfortunate; changing this
          will require very careful checking everywhere and may mean having
          to rewrite some unit test code.
*/
bool
to_currency(Currency&, std::string const&);

/** Tries to convert a string to a Currency, returns noCurrency() on failure.

    @note This function can return badCurrency(). This legacy behavior is
          unfortunate; changing this will require very careful checking
          everywhere and may mean having to rewrite some unit test code.
*/
Currency
to_currency(std::string const&);

inline std::ostream&
operator<<(std::ostream& os, Currency const& x)
{
    os << to_string(x);
    return os;
}

}  // namespace ripple

namespace std {

template <>
struct hash<ripple::Currency> : ripple::Currency::hasher
{
    explicit hash() = default;
};

template <>
struct hash<ripple::NodeID> : ripple::NodeID::hasher
{
    explicit hash() = default;
};

template <>
struct hash<ripple::Directory> : ripple::Directory::hasher
{
    explicit hash() = default;
};

}  // namespace std

#endif
