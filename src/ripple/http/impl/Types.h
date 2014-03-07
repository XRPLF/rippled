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

#ifndef RIPPLE_HTTP_TYPES_H_INCLUDED
#define RIPPLE_HTTP_TYPES_H_INCLUDED

namespace ripple {
namespace HTTP {

typedef boost::system::error_code error_code;
typedef boost::asio::ip::tcp Protocol;
typedef boost::asio::ip::address address;
typedef Protocol::endpoint endpoint_t;
typedef Protocol::acceptor acceptor;
typedef Protocol::socket socket;

inline std::string to_string (address const& addr)
{
    return addr.to_string();
}

inline std::string to_string (endpoint_t const& endpoint)
{
    std::stringstream ss;
    ss << to_string (endpoint.address());
    if (endpoint.port() != 0)
        ss << ":" << std::dec << endpoint.port();
    return std::string (ss.str());
}

inline endpoint_t to_asio (Port const& port)
{
    if (port.addr.is_v4())
    {
        beast::IP::AddressV4 v4 (port.addr.to_v4());
        std::string const& s (to_string (v4));
        return endpoint_t (address().from_string (s), port.port);
    }

    //IP::Endpoint::V6 v6 (ep.v6());
    return endpoint_t ();
}

inline beast::IP::Endpoint from_asio (endpoint_t const& endpoint)
{
    std::stringstream ss (to_string (endpoint));
    beast::IP::Endpoint ep;
    ss >> ep;
    return ep;
}

}
}

#endif
