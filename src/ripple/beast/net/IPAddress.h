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

#include <ripple/beast/net/IPAddressV4.h>
#include <ripple/beast/net/IPAddressV6.h>
#include <ripple/beast/hash/hash_append.h>
#include <ripple/beast/hash/uhash.h>
#include <boost/functional/hash.hpp>
#include <cassert>
#include <cstdint>
#include <ios>
#include <string>
#include <sstream>
#include <typeinfo>

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
    Address ()
        : m_type (ipv4)
    {
    }

    /** Create an IPv4 address. */
    Address (AddressV4 const& addr)
        : m_type (ipv4)
        , m_v4 (addr)
    {
    }

    /** Create an IPv6 address. */
    Address (AddressV6 const& addr)
        : m_type (ipv6)
        , m_v6 (addr)
    {
    }

    /** Assign a copy from another address in any format. */
    /** @{ */
    Address&
    operator= (AddressV4 const& addr)
    {
        m_type = ipv4;
        m_v6 = AddressV6();
        m_v4 = addr;
        return *this;
    }

    Address&
    operator= (AddressV6 const& addr)
    {
        m_type = ipv6;
        m_v4 = AddressV4();
        m_v6 = addr;
        return *this;
    }
    /** @} */

    /** Create an Address from a string.
        @return A pair with the address, and bool set to `true` on success.
    */
    static
    std::pair <Address, bool>
    from_string (std::string const& s);

    /** Returns a string representing the address. */
    std::string
    to_string () const
    {
        return (is_v4 ())
            ? IP::to_string (to_v4())
            : IP::to_string (to_v6());
    }

    /** Returns `true` if this address represents an IPv4 address. */
    bool
    is_v4 () const noexcept
    {
        return m_type == ipv4;
    }

    /** Returns `true` if this address represents an IPv6 address. */
    bool
    is_v6() const noexcept
    {
        return m_type == ipv6;
    }

    /** Returns the IPv4 address.
        Precondition:
            is_v4() == `true`
    */
    AddressV4 const&
    to_v4 () const
    {
        if (!is_v4 ())
            throw std::bad_cast();
        return m_v4;
    }

    /** Returns the IPv6 address.
        Precondition:
            is_v6() == `true`
    */
    AddressV6 const&
    to_v6 () const
    {
        if (!is_v6 ())
            throw std::bad_cast();
        return m_v6;
    }

    /** Returns `true` if this address represents 0.0.0.0 */
    bool
    is_any () const
    {
        return is_v4 () ? m_v4 == IP::AddressV4::any ()
            : false; // m_v6 == IP::AddressV6::any();
    }

    template <class Hasher>
    friend
    void
    hash_append(Hasher& h, Address const& addr) noexcept
    {
        using beast::hash_append;
        if (addr.is_v4 ())
            hash_append(h, addr.to_v4 ());
        else if (addr.is_v6 ())
            hash_append(h, addr.to_v6 ());
        else
            assert (false);
    }

    /** Arithmetic comparison. */
    /** @{ */
    friend
    bool
    operator== (Address const& lhs, Address const& rhs)
    {
        if (lhs.is_v4 ())
        {
            if (rhs.is_v4 ())
                return lhs.to_v4() == rhs.to_v4();
        }
        else
        {
            if (rhs.is_v6 ())
                return lhs.to_v6() == rhs.to_v6();
        }

        return false;
    }

    friend
    bool
    operator< (Address const& lhs, Address const& rhs)
    {
        if (lhs.m_type < rhs.m_type)
            return true;
        if (lhs.is_v4 ())
            return lhs.to_v4() < rhs.to_v4();
        return lhs.to_v6() < rhs.to_v6();
    }

    friend
    bool
    operator!= (Address const& lhs, Address const& rhs)
    {
        return ! (lhs == rhs);
    }

    friend
    bool
    operator>  (Address const& lhs, Address const& rhs)
    {
        return rhs < lhs;
    }

    friend
    bool
    operator<= (Address const& lhs, Address const& rhs)
    {
        return ! (lhs > rhs);
    }

    friend
    bool
    operator>= (Address const& lhs, Address const& rhs)
    {
        return ! (rhs > lhs);
    }
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
inline
bool
is_loopback (Address const& addr)
{
    return (addr.is_v4 ())
        ? is_loopback (addr.to_v4 ())
        : is_loopback (addr.to_v6 ());
}

/** Returns `true` if the address is unspecified. */
inline
bool
is_unspecified (Address const& addr)
{
    return (addr.is_v4 ())
        ? is_unspecified (addr.to_v4 ())
        : is_unspecified (addr.to_v6 ());
}

/** Returns `true` if the address is a multicast address. */
inline
bool
is_multicast (Address const& addr)
{
    return (addr.is_v4 ())
        ? is_multicast (addr.to_v4 ())
        : is_multicast (addr.to_v6 ());
}

/** Returns `true` if the address is a private unroutable address. */
inline
bool
is_private (Address const& addr)
{
    return (addr.is_v4 ())
        ? is_private (addr.to_v4 ())
        : is_private (addr.to_v6 ());
}

/** Returns `true` if the address is a public routable address. */
inline
bool
is_public (Address const& addr)
{
    return (addr.is_v4 ())
        ? is_public (addr.to_v4 ())
        : is_public (addr.to_v6 ());
}

//------------------------------------------------------------------------------

/** Returns the address represented as a string. */
inline std::string to_string (Address const& addr)
{
    return addr.to_string ();
}

/** Output stream conversion. */
template <typename OutputStream>
OutputStream&
operator<< (OutputStream& os, Address const& addr)
{
    return os << to_string (addr);
}

/** Input stream conversion. */
inline
std::istream&
operator>> (std::istream& is, Address& addr)
{
    // VFALCO TODO Support ipv6!
    AddressV4 addrv4;
    is >> addrv4;
    addr = Address (addrv4);
    return is;
}

inline
std::pair <Address, bool>
Address::from_string (std::string const& s)
{
    std::stringstream is (s);
    Address addr;
    is >> addr;
    if (! is.fail() && is.rdbuf()->in_avail() == 0)
        return std::make_pair (addr, true);
    return std::make_pair (Address (), false);
}

}
}

//------------------------------------------------------------------------------

namespace std {
template <>
struct hash <beast::IP::Address>
{
    std::size_t
    operator() (beast::IP::Address const& addr) const
    {
        return beast::uhash<>{} (addr);
    }
};
}

namespace boost {
template <>
struct hash <::beast::IP::Address>
{
    std::size_t
    operator() (::beast::IP::Address const& addr) const
    {
        return ::beast::uhash<>{} (addr);
    }
};
}

#endif
