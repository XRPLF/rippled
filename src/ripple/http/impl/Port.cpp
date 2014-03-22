//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

namespace ripple {
namespace HTTP {

Port::Port ()
    : port (0)
    , security (no_ssl)
    , context (nullptr)
{
}

Port::Port (Port const& other)
    : port (other.port)
    , addr (other.addr)
    , security (other.security)
    , context (other.context)
{
}

Port& Port::operator= (Port const& other)
{
    port = other.port;
    addr = other.addr;
    security = other.security;
    context = other.context;
    return *this;
}

Port::Port (
    std::uint16_t port_,
    beast::IP::Endpoint const& addr_,
    Security security_,
    beast::asio::SSLContext* context_)
    : port (port_)
    , addr (addr_)
    , security (security_)
    , context (context_)
{
}

bool operator== (Port const& lhs, Port const& rhs)
{
    if (lhs.addr != rhs.addr)
        return false;
    if (lhs.port != rhs.port)
        return false;
    if (lhs.security != rhs.security)
        return false;
    // 'context' does not participate in the comparison
    return true;
}

bool operator< (Port const& lhs, Port const& rhs)
{
    if (lhs.addr > rhs.addr)
        return false;
    else if (lhs.addr < rhs.addr)
        return true;

    if (lhs.port > rhs.port)
        return false;
    else if (lhs.port < rhs.port)
        return true;

    if (lhs.security > rhs.security)
        return false;
    else if (lhs.security < rhs.security)
        return true;

    return true;
}

bool operator!= (Port const& lhs, Port const& rhs)
    { return ! (lhs == rhs); }

bool operator> (Port const& lhs, Port const& rhs)
    { return rhs < lhs; }

bool operator<= (Port const& lhs, Port const& rhs)
    { return ! (rhs < lhs); }

bool operator>= (Port const& lhs, Port const& rhs)
    { return ! (lhs < rhs); }


}
}
