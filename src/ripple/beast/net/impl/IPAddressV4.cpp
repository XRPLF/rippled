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
#endif

#include <ripple/beast/net/IPAddressV4.h>
#include <ripple/beast/net/detail/Parse.h>

#include <sstream>
#include <stdexcept>

namespace beast {
namespace IP {

bool is_private (AddressV4 const& addr)
{
    return
        ((addr.to_ulong() & 0xff000000) == 0x0a000000) || // Prefix /8,    10.  #.#.#
        ((addr.to_ulong() & 0xfff00000) == 0xac100000) || // Prefix /12   172. 16.#.# - 172.31.#.#
        ((addr.to_ulong() & 0xffff0000) == 0xc0a80000) || // Prefix /16   192.168.#.#
        addr.is_loopback();
}

bool is_public (AddressV4 const& addr)
{
    return
        ! is_private (addr) &&
        ! addr.is_multicast();
}

char get_class (AddressV4 const& addr)
{
    static char const* table = "AAAABBCD";
    return table [(addr.to_ulong() & 0xE0000000) >> 29];
}

}
}
