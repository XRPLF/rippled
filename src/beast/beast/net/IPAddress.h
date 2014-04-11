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

#ifndef BEAST_NET_IPADDRESS_H_INCLUDED
#define BEAST_NET_IPADDRESS_H_INCLUDED

#include <string>
#include <ios>
#include <sstream>
   
#include "../CStdInt.h"
#include "IPAddressV4.h"
#include "IPAddressV6.h"

//------------------------------------------------------------------------------

namespace beast {
namespace IP {

/** A version-independent IP address.
    This object can represent either an IPv4 or IPv6 address.
*/
class Address
{
public:
    /** Create an unspecified IPv4 address. */
    Address ();

    /** Create an IPv4 address. */
    Address (AddressV4 const& addr);

    /** Create an IPv6 address. */
    Address (AddressV6 const& addr);

    /** Assign a copy from another address in any format. */
    /** @{ */
    Address& operator= (AddressV4 const& addr);
    Address& operator= (AddressV6 const& addr);
    /** @} */

    /** Create an Address from a string.
        @return A pair with the address, and bool set to `true` on success.
    */
    static std::pair <Address, bool> from_string (std::string const& s);

    /** Returns a string representing the address. */
    std::string to_string () const;

    /** Returns `true` if this address represents an IPv4 address. */
    bool is_v4 () const
        { return m_type == ipv4; }

    /** Returns `true` if this address represents an IPv6 address. */
    bool is_v6 () const
        { return m_type == ipv6; }

    /** Returns the IPv4 address.
        Precondition:
            is_v4() returns true
    */
    AddressV4 const& to_v4 () const;

    /** Returns the IPv6 address.
        Precondition:
            is_v6() returns true
    */
    AddressV6 const& to_v6 () const;

    /** Arithmetic comparison. */
    /** @{ */
    friend bool operator== (Address const& lhs, Address const& rhs);
    friend bool operator<  (Address const& lhs, Address const& rhs);

    friend bool operator!= (Address const& lhs, Address const& rhs)
        { return ! (lhs == rhs); }
    friend bool operator>  (Address const& lhs, Address const& rhs)
        { return rhs < lhs; }
    friend bool operator<= (Address const& lhs, Address const& rhs)
        { return ! (lhs > rhs); }
    friend bool operator>= (Address const& lhs, Address const& rhs)
        { return ! (rhs > lhs); }
    /** @} */

private:
    enum Type
    {
        ipv4,
        ipv6
    };

    Type m_type;
    AddressV4 m_v4;
    AddressV6 m_v6;
};

//------------------------------------------------------------------------------

// Properties

/** Returns `true` if this is a loopback address. */
bool is_loopback (Address const& addr);

/** Returns `true` if the address is unspecified. */
bool is_unspecified (Address const& addr);

/** Returns `true` if the address is a multicast address. */
bool is_multicast (Address const& addr);

/** Returns `true` if the address is a private unroutable address. */
bool is_private (Address const& addr);

/** Returns `true` if the address is a public routable address. */
bool is_public (Address const& addr);

//------------------------------------------------------------------------------

/** boost::hash support. */
std::size_t hash_value (Address const& addr);

/** Returns the address represented as a string. */
inline std::string to_string (Address const& addr)
    { return addr.to_string (); }

/** Output stream conversion. */
template <typename OutputStream>
OutputStream& operator<< (OutputStream& os, Address const& addr)
    { return os << to_string (addr); }

/** Input stream conversion. */
std::istream& operator>> (std::istream& is, Address& addr);

}
}

//------------------------------------------------------------------------------

namespace std {
template <>
struct hash <beast::IP::Address>
{
    std::size_t operator() (beast::IP::Address const& addr) const
        { return hash_value (addr); }
};
}

#endif
