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

#include <ripple/beast/net/IPEndpoint.h>
#include <ripple/beast/net/detail/Parse.h>

namespace beast {
namespace IP {

Endpoint::Endpoint ()
    : m_port (0)
{
}

Endpoint::Endpoint (Address const& addr, Port port)
    : m_addr (addr)
    , m_port (port)
{
}

std::pair <Endpoint, bool> Endpoint::from_string_checked (std::string const& s)
{
    std::stringstream is (s);
    Endpoint endpoint;
    is >> endpoint;
    if (! is.fail() && is.rdbuf()->in_avail() == 0)
        return std::make_pair (endpoint, true);
    return std::make_pair (Endpoint (), false);
}

Endpoint Endpoint::from_string (std::string const& s)
{
    std::pair <Endpoint, bool> const result (
        from_string_checked (s));
    if (result.second)
        return result.first;
    return Endpoint();
}

// VFALCO NOTE This is a hack to support legacy data format
//
Endpoint Endpoint::from_string_altform (std::string const& s)
{
    // Accept the regular form if it parses
    {
        Endpoint ep (Endpoint::from_string (s));
        if (! is_unspecified (ep))
            return ep;
    }

    // Now try the alt form
    std::stringstream is (s);

    AddressV4 v4;
    is >> v4;
    if (! is.fail())
    {
        Endpoint ep (v4);

        if (is.rdbuf()->in_avail()>0)
        {
            if (! IP::detail::expect_whitespace (is))
                return Endpoint();

            while (is.rdbuf()->in_avail()>0)
            {
                char c;
                is.get(c);
                if (!isspace (static_cast<unsigned char>(c)))
                {
                    is.unget();
                    break;
                }
            }

            Port port;
            is >> port;
            if (is.fail())
                return Endpoint();

            return ep.at_port (port);
        }
        else
        {
            // Just an address with no port
            return ep;
        }
    }

    // Could be V6 here...

    return Endpoint();
}

std::string Endpoint::to_string () const
{
    std::string s (address ().to_string ());
    if (port() != 0)
        s = s + ":" + std::to_string (port());
    return s;
}

bool operator== (Endpoint const& lhs, Endpoint const& rhs)
{
    return lhs.address() == rhs.address() &&
           lhs.port() == rhs.port();
}

bool operator<  (Endpoint const& lhs, Endpoint const& rhs)
{
    if (lhs.address() < rhs.address())
        return true;
    if (lhs.address() > rhs.address())
        return false;
    return lhs.port() < rhs.port();
}

//------------------------------------------------------------------------------

std::istream& operator>> (std::istream& is, Endpoint& endpoint)
{
    // VFALCO TODO Support ipv6!

    Address addr;
    is >> addr;
    if (is.fail())
        return is;

    if (is.rdbuf()->in_avail()>0)
    {
        char c;
        is.get(c);
        if (c != ':')
        {
            is.unget();
            endpoint = Endpoint (addr);
            return is;
        }

        Port port;
        is >> port;
        if (is.fail())
            return is;

        endpoint = Endpoint (addr, port);
        return is;
    }

    endpoint = Endpoint (addr);
    return is;
}

}
}
