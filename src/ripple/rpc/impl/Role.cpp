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

#include <ripple/rpc/Role.h>
#include <boost/beast/core/string.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/rfc7230.hpp>
#include <boost/utility/string_view.hpp>
#include <algorithm>

namespace ripple {

bool
passwordUnrequiredOrSentCorrect (Port const& port,
                                 Json::Value const& params) {

    assert(! port.admin_ip.empty ());
    bool const passwordRequired = (!port.admin_user.empty() ||
                                   !port.admin_password.empty());

    return !passwordRequired  ||
            ((params["admin_password"].isString() &&
              params["admin_password"].asString() == port.admin_password) &&
             (params["admin_user"].isString() &&
              params["admin_user"].asString() == port.admin_user));
}

bool
ipAllowed (beast::IP::Address const& remoteIp,
           std::vector<beast::IP::Address> const& adminIp)
{
    return std::find_if (adminIp.begin (), adminIp.end (),
        [&remoteIp](beast::IP::Address const& ip) { return ip.is_unspecified () ||
            ip == remoteIp; }) != adminIp.end ();
}

bool
isAdmin (Port const& port, Json::Value const& params,
         beast::IP::Address const& remoteIp)
{
    return ipAllowed (remoteIp, port.admin_ip) &&
        passwordUnrequiredOrSentCorrect (port, params);
}

Role
requestRole (Role const& required, Port const& port,
             Json::Value const& params, beast::IP::Endpoint const& remoteIp,
             boost::string_view const& user)
{
    if (isAdmin(port, params, remoteIp.address()))
        return Role::ADMIN;

    if (required == Role::ADMIN)
        return Role::FORBID;

    if (ipAllowed(remoteIp.address(), port.secure_gateway_ip))
    {
        if (user.size())
            return Role::IDENTIFIED;
        return Role::PROXY;
    }

    return Role::GUEST;
}

/**
 * ADMIN and IDENTIFIED roles shall have unlimited resources.
 */
bool
isUnlimited (Role const& role)
{
    return role == Role::ADMIN || role == Role::IDENTIFIED;
}

bool
isUnlimited (Role const& required, Port const& port,
    Json::Value const& params, beast::IP::Endpoint const& remoteIp,
    std::string const& user)
{
    return isUnlimited(requestRole(required, port, params, remoteIp, user));
}

Resource::Consumer
requestInboundEndpoint (Resource::Manager& manager,
    beast::IP::Endpoint const& remoteAddress, Role const& role,
    boost::string_view const& user, boost::string_view const& forwardedFor)
{
    if (isUnlimited(role))
        return manager.newUnlimitedEndpoint (remoteAddress);

    return manager.newInboundEndpoint(remoteAddress, role == Role::PROXY,
        forwardedFor);
}

boost::string_view
forwardedFor(http_request_type const& request)
{
    auto it = request.find(boost::beast::http::field::forwarded);
    if (it != request.end())
    {
        auto ascii_tolower = [](char c) -> char
        {
            return ((static_cast<unsigned>(c) - 65U) < 26) ?
                    c + 'a' - 'A' : c;
        };

        static std::string const forStr{"for="};
        auto found = std::search(it->value().begin(), it->value().end(),
            forStr.begin(), forStr.end(),
            [&ascii_tolower](char c1, char c2)
            {
                return ascii_tolower(c1) == ascii_tolower(c2);
            }
        );

        if (found == it->value().end())
            return {};

        found += forStr.size();
        std::size_t const pos ([&]()
        {
            std::size_t const pos{boost::string_view(
                found, it->value().end() - found).find(';')};
            if (pos == boost::string_view::npos)
                return it->value().size() - forStr.size();
            return pos;
        }());

        return *boost::beast::http::token_list(
            boost::string_view(found, pos)).begin();
    }

    it = request.find("X-Forwarded-For");
    if (it != request.end())
    {
        return *boost::beast::http::token_list(it->value()).begin();
    }

    return {};
}

}
