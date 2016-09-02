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

#if BEAST_INCLUDE_BEASTCONFIG
#include "../../BeastConfig.h"
#endif

#include <ripple/beast/net/IPAddressV4.h>
#include <ripple/beast/net/detail/Parse.h>

#include <sstream>
#include <stdexcept>

namespace beast {
namespace IP {

AddressV4::AddressV4 ()
    : value (0)
{
}

AddressV4::AddressV4 (std::uint32_t value_)
    : value (value_)
{
}

AddressV4::AddressV4 (std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d)
    : value ((a<<24)|(b<<16)|(c<<8)|d)
{
}

std::pair <AddressV4, bool> AddressV4::from_string (std::string const& s)
{
    std::stringstream is (s);
    AddressV4 addr;
    is >> addr;
    if (! is.fail() && is.rdbuf()->in_avail() == 0)
        return std::make_pair (addr, true);
    return std::make_pair (AddressV4 (), false);
}

AddressV4 AddressV4::broadcast (AddressV4 const& address)
{
    return broadcast (address, netmask (address));
}

AddressV4 AddressV4::broadcast (
    AddressV4 const& address, AddressV4 const& mask)
{
    return AddressV4 (address.value | (mask.value ^ 0xffffffff));
}

char AddressV4::get_class (AddressV4 const& addr)
{
    static char const* table = "AAAABBCD";
    return table [(addr.value & 0xE0000000) >> 29];
}

AddressV4 AddressV4::netmask (char address_class)
{
    switch (address_class)
    {
    case 'A': return AddressV4 (0xff000000);
    case 'B': return AddressV4 (0xffff0000);
    case 'C': return AddressV4 (0xffffff00);
    case 'D':
    default:
        break;
    }
    return AddressV4 (0xffffffff);
}

AddressV4 AddressV4::netmask (AddressV4 const& addr)
{
    return netmask (get_class (addr));
}

AddressV4::Proxy <true> AddressV4::operator[] (std::size_t index) const
{
    switch (index)
    {
    default:
        throw std::out_of_range ("bad array index");
    case 0: return Proxy <true> (24, &value);
    case 1: return Proxy <true> (16, &value);
    case 2: return Proxy <true> ( 8, &value);
    case 3: return Proxy <true> ( 0, &value);
    };
};

AddressV4::Proxy <false> AddressV4::operator[] (std::size_t index)
{
    switch (index)
    {
    default:
        throw std::out_of_range ("bad array index");
    case 0: return Proxy <false> (24, &value);
    case 1: return Proxy <false> (16, &value);
    case 2: return Proxy <false> ( 8, &value);
    case 3: return Proxy <false> ( 0, &value);
    };
};

//------------------------------------------------------------------------------

bool is_loopback (AddressV4 const& addr)
{
    return (addr.value & 0xff000000) == 0x7f000000;
}

bool is_unspecified (AddressV4 const& addr)
{
    return addr.value == 0;
}

bool is_multicast (AddressV4 const& addr)
{
    return (addr.value & 0xf0000000) == 0xe0000000;
}

bool is_private (AddressV4 const& addr)
{
    return
        ((addr.value & 0xff000000) == 0x0a000000) || // Prefix /8,    10.  #.#.#
        ((addr.value & 0xfff00000) == 0xac100000) || // Prefix /12   172. 16.#.# - 172.31.#.#
        ((addr.value & 0xffff0000) == 0xc0a80000) || // Prefix /16   192.168.#.#
        is_loopback (addr);
}

bool is_public (AddressV4 const& addr)
{
    return
        ! is_private (addr) &&
        ! is_multicast (addr);
}

//------------------------------------------------------------------------------

std::string to_string (AddressV4 const& addr)
{
    std::string s;
    s.reserve (15);
    s =
        std::to_string (addr[0]) + "." +
        std::to_string (addr[1]) + "." +
        std::to_string (addr[2]) + "." +
        std::to_string (addr[3]);
    return s;
}

std::istream& operator>> (std::istream& is, AddressV4& addr)
{
    std::uint8_t octet [4];
    is >> IP::detail::integer (octet [0]);
    for (int i = 1; i < 4; ++i)
    {
        if (!is || !IP::detail::expect(is, '.'))
            return is;
        is >> IP::detail::integer (octet [i]);
        if (!is)
            return is;
    }
    addr = AddressV4 (octet[0], octet[1], octet[2], octet[3]);
    return is;
}

}
}
