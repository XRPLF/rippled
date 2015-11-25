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

#ifndef RIPPLE_SERVER_PORT_H_INCLUDED
#define RIPPLE_SERVER_PORT_H_INCLUDED

#include <beast/net/IPEndpoint.h>
#include <beast/utility/ci_char_traits.h>
#include <boost/asio/ip/address.hpp>
#include <cstdint>
#include <memory>
#include <set>
#include <string>

namespace boost { namespace asio { namespace ssl { class context; } } }

namespace ripple {
namespace HTTP {

/** Configuration information for a Server listening port. */
struct Port
{
    std::string name;
    boost::asio::ip::address ip;
    std::uint16_t port = 0;
    std::set<std::string, beast::ci_less> protocol;
    std::vector<beast::IP::Address> admin_ip;
    std::vector<beast::IP::Address> secure_gateway_ip;
    std::string user;
    std::string password;
    std::string admin_user;
    std::string admin_password;
    std::string ssl_key;
    std::string ssl_cert;
    std::string ssl_chain;
    std::shared_ptr<boost::asio::ssl::context> context;

    // Returns `true` if any websocket protocols are specified
    template <class = void>
    bool
    websockets() const;

    // Returns `true` if any secure protocols are specified
    template <class = void>
    bool
    secure() const;

    // Returns a string containing the list of protocols
    template <class = void>
    std::string
    protocols() const;
};

//------------------------------------------------------------------------------

template <class>
bool
Port::websockets() const
{
    return protocol.count("ws") > 0 || protocol.count("wss") > 0;
}

template <class>
bool
Port::secure() const
{
    return protocol.count("peer") > 0 ||
        protocol.count("https") > 0 || protocol.count("wss") > 0;
}

template <class>
std::string
Port::protocols() const
{
    std::string s;
    for (auto iter = protocol.cbegin();
            iter != protocol.cend(); ++iter)
        s += (iter != protocol.cbegin() ? "," : "") + *iter;
    return s;
}

std::ostream&
operator<< (std::ostream& os, Port const& p);

} // HTTP
} // ripple

#endif
