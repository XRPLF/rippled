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
    return std::make_pair (Endpoint {}, false);
}

Endpoint Endpoint::from_string (std::string const& s)
{
    std::pair <Endpoint, bool> const result (
        from_string_checked (s));
    if (result.second)
        return result.first;
    return Endpoint {};
}

std::string Endpoint::to_string () const
{
    std::string s;
    s.reserve(
        (address().is_v6() ? INET6_ADDRSTRLEN-1 : 15) +
        (port() == 0 ? 0 : 6 + (address().is_v6() ? 2 : 0)));

    if (port() != 0 && address().is_v6())
        s += '[';
    s += address ().to_string();
    if (port())
    {
        if (address().is_v6())
            s += ']';
        s += ":" + std::to_string (port());
    }

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
    std::string addrStr;
    // valid addresses only need INET6_ADDRSTRLEN-1 chars, but allow the extra
    // char to check for invalid lengths
    addrStr.reserve(INET6_ADDRSTRLEN);
    char i {0};
    char readTo {0};
    is.get(i);
    if (i == '[') // we are an IPv6 endpoint
        readTo = ']';
    else
        addrStr+=i;

    while (is && is.rdbuf()->in_avail() > 0 && is.get(i))
    {
        // NOTE: There is a legacy data format
        // that allowed space to be used as address / port separator
        // so we continue to honor that here by assuming we are at the end
        // of the address portion if we hit a space (or the separator
        // we were expecting to see)
        if (isspace(i) || (readTo && i == readTo))
            break;

        if ((i == '.') ||
            (i >= '0' && i <= ':') ||
            (i >= 'a' && i <= 'f') ||
            (i >= 'A' && i <= 'F'))
        {
            addrStr+=i;

            // don't exceed a reasonable length...
            if ( addrStr.size() == INET6_ADDRSTRLEN ||
                (readTo && readTo == ':' && addrStr.size() > 15))
            {
                is.setstate (std::ios_base::failbit);
                return is;
            }

            if (! readTo && (i == '.' || i == ':'))
            {
                // if we see a dot first, must be IPv4
                // otherwise must be non-bracketed IPv6
                readTo = (i == '.') ? ':' : ' ';
            }
        }
        else // invalid char
        {
            is.unget();
            is.setstate (std::ios_base::failbit);
            return is;
        }
    }

    if (readTo == ']' && is.rdbuf()->in_avail() > 0)
    {
        is.get(i);
        if (! (isspace(i) || i == ':'))
        {
            is.unget();
            is.setstate (std::ios_base::failbit);
            return is;
        }
    }

    boost::system::error_code ec;
    auto addr = boost::asio::ip::make_address(addrStr, ec);
    if (ec)
    {
        is.setstate (std::ios_base::failbit);
        return is;
    }

    if (is.rdbuf()->in_avail() > 0)
    {
        Port port;
        is >> port;
        if (is.fail())
            return is;
        endpoint = Endpoint (addr, port);
    }
    else
        endpoint = Endpoint (addr);

    return is;
}

}
}
