//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_NET_IPADDRESSV6_H_INCLUDED
#define BEAST_NET_IPADDRESSV6_H_INCLUDED

#include <cassert>
#include <cstdint>
#include <functional>
#include <ios>
#include <string>
#include <utility>

namespace beast {
namespace IP {

/** Represents a version 4 IP address. */
struct AddressV6
{
    // VFALCO TODO

    /** Arithmetic comparison. */
    /** @{ */
    friend bool operator== (AddressV6 const&, AddressV6 const&)
        { assert(false); return false; }
    friend bool operator<  (AddressV6 const&, AddressV6 const&)
        { assert(false); return false; }

    friend bool operator!= (AddressV6 const& lhs, AddressV6 const& rhs)
        { return ! (lhs == rhs); }
    friend bool operator>  (AddressV6 const& lhs, AddressV6 const& rhs)
        { return rhs < lhs; }
    friend bool operator<= (AddressV6 const& lhs, AddressV6 const& rhs)
        { return ! (lhs > rhs); }
    friend bool operator>= (AddressV6 const& lhs, AddressV6 const& rhs)
        { return ! (rhs > lhs); }
    /** @} */
};

//------------------------------------------------------------------------------

/** Returns `true` if this is a loopback address. */
bool is_loopback (AddressV6 const& addr);

/** Returns `true` if the address is unspecified. */
bool is_unspecified (AddressV6 const& addr);

/** Returns `true` if the address is a multicast address. */
bool is_multicast (AddressV6 const& addr);

/** Returns `true` if the address is a private unroutable address. */
bool is_private (AddressV6 const& addr);

/** Returns `true` if the address is a public routable address. */
bool is_public (AddressV6 const& addr);

//------------------------------------------------------------------------------

template <class Hasher>
void
hash_append(Hasher&, AddressV6 const&)
{
    assert(false);
}

/** Returns the address represented as a string. */
std::string to_string (AddressV6 const& addr);

/** Output stream conversion. */
template <typename OutputStream>
OutputStream& operator<< (OutputStream& os, AddressV6 const& addr)
    { return os << to_string (addr); }

/** Input stream conversion. */
std::istream& operator>> (std::istream& is, AddressV6& addr);

}
}

//------------------------------------------------------------------------------

namespace std {
/** std::hash support. */
template <>
struct hash <beast::IP::AddressV6>
{
    std::size_t operator() (beast::IP::AddressV6 const& addr) const
        { assert(false); return 0; }
};
}

#endif
