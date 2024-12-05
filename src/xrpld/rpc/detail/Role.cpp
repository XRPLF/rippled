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

#include <xrpld/rpc/Role.h>
#include <boost/beast/http/field.hpp>
#include <boost/utility/string_view.hpp>
#include <algorithm>
#include <tuple>

namespace ripple {

bool
passwordUnrequiredOrSentCorrect(Port const& port, Json::Value const& params)
{
    ASSERT(
        !(port.admin_nets_v4.empty() && port.admin_nets_v6.empty()),
        "ripple::passwordUnrequiredOrSentCorrect : non-empty admin nets");
    bool const passwordRequired =
        (!port.admin_user.empty() || !port.admin_password.empty());

    return !passwordRequired ||
        ((params["admin_password"].isString() &&
          params["admin_password"].asString() == port.admin_password) &&
         (params["admin_user"].isString() &&
          params["admin_user"].asString() == port.admin_user));
}

bool
ipAllowed(
    beast::IP::Address const& remoteIp,
    std::vector<boost::asio::ip::network_v4> const& nets4,
    std::vector<boost::asio::ip::network_v6> const& nets6)
{
    // To test whether the remoteIP is part of one of the configured
    // subnets, first convert it to a subnet definition. For ipv4,
    // this means appending /32. For ipv6, /128. Then based on protocol
    // check for whether the resulting network is either a subnet of or
    // equal to each configured subnet, based on boost::asio's reasoning.
    // For example, 10.1.2.3 is a subnet of 10.1.2.0/24, but 10.1.2.0 is
    // not. However, 10.1.2.0 is equal to the network portion of 10.1.2.0/24.

    std::string addrString = remoteIp.to_string();
    if (remoteIp.is_v4())
    {
        addrString += "/32";
        auto ipNet = boost::asio::ip::make_network_v4(addrString);
        for (auto const& net : nets4)
        {
            if (ipNet.is_subnet_of(net) || ipNet == net)
                return true;
        }
    }
    else
    {
        addrString += "/128";
        auto ipNet = boost::asio::ip::make_network_v6(addrString);
        for (auto const& net : nets6)
        {
            if (ipNet.is_subnet_of(net) || ipNet == net)
                return true;
        }
    }

    return false;
}

bool
isAdmin(
    Port const& port,
    Json::Value const& params,
    beast::IP::Address const& remoteIp)
{
    return ipAllowed(remoteIp, port.admin_nets_v4, port.admin_nets_v6) &&
        passwordUnrequiredOrSentCorrect(port, params);
}

Role
requestRole(
    Role const& required,
    Port const& port,
    Json::Value const& params,
    beast::IP::Endpoint const& remoteIp,
    std::string_view user)
{
    if (isAdmin(port, params, remoteIp.address()))
        return Role::ADMIN;

    if (required == Role::ADMIN)
        return Role::FORBID;

    if (ipAllowed(
            remoteIp.address(),
            port.secure_gateway_nets_v4,
            port.secure_gateway_nets_v6))
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
isUnlimited(Role const& role)
{
    return role == Role::ADMIN || role == Role::IDENTIFIED;
}

bool
isUnlimited(
    Role const& required,
    Port const& port,
    Json::Value const& params,
    beast::IP::Endpoint const& remoteIp,
    std::string const& user)
{
    return isUnlimited(requestRole(required, port, params, remoteIp, user));
}

Resource::Consumer
requestInboundEndpoint(
    Resource::Manager& manager,
    beast::IP::Endpoint const& remoteAddress,
    Role const& role,
    std::string_view user,
    std::string_view forwardedFor)
{
    if (isUnlimited(role))
        return manager.newUnlimitedEndpoint(remoteAddress);

    return manager.newInboundEndpoint(
        remoteAddress, role == Role::PROXY, forwardedFor);
}

static std::string_view
extractIpAddrFromField(std::string_view field)
{
    // Lambda to trim leading and trailing spaces on the field.
    auto trim = [](std::string_view str) -> std::string_view {
        std::string_view ret = str;

        // Only do the work if there's at least one leading space.
        if (!ret.empty() && ret.front() == ' ')
        {
            std::size_t const firstNonSpace = ret.find_first_not_of(' ');
            if (firstNonSpace == std::string_view::npos)
                // We know there's at least one leading space.  So if we got
                // npos, then it must be all spaces.  Return empty string_view.
                return {};

            ret = ret.substr(firstNonSpace);
        }
        // Trim trailing spaces.
        if (!ret.empty())
        {
            // Only do the work if there's at least one trailing space.
            if (unsigned char const c = ret.back();
                c == ' ' || c == '\r' || c == '\n')
            {
                std::size_t const lastNonSpace = ret.find_last_not_of(" \r\n");
                if (lastNonSpace == std::string_view::npos)
                    // We know there's at least one leading space.  So if we
                    // got npos, then it must be all spaces.
                    return {};

                ret = ret.substr(0, lastNonSpace + 1);
            }
        }
        return ret;
    };

    std::string_view ret = trim(field);
    if (ret.empty())
        return {};

    // If there are surrounding quotes, strip them.
    if (ret.front() == '"')
    {
        ret.remove_prefix(1);
        if (ret.empty() || ret.back() != '"')
            return {};  // Unbalanced double quotes.

        ret.remove_suffix(1);

        // Strip leading and trailing spaces that were inside the quotes.
        ret = trim(ret);
    }
    if (ret.empty())
        return {};

    // If we have an IPv6 or IPv6 (dual) address wrapped in square brackets,
    // then we need to remove the square brackets.
    if (ret.front() == '[')
    {
        // Remove leading '['.
        ret.remove_prefix(1);

        // We may have an IPv6 address in square brackets.  Scan up to the
        // closing square bracket.
        auto const closeBracket =
            std::find_if_not(ret.begin(), ret.end(), [](unsigned char c) {
                return std::isxdigit(c) || c == ':' || c == '.' || c == ' ';
            });

        // If the string does not close with a ']', then it's not valid IPv6
        // or IPv6 (dual).
        if (closeBracket == ret.end() || (*closeBracket) != ']')
            return {};

        // Remove trailing ']'
        ret = ret.substr(0, closeBracket - ret.begin());
        ret = trim(ret);
    }
    if (ret.empty())
        return {};

    // If this is an IPv6 address (after unwrapping from square brackets),
    // then there cannot be an appended port.  In that case we're done.
    {
        // Skip any leading hex digits.
        auto const colon =
            std::find_if_not(ret.begin(), ret.end(), [](unsigned char c) {
                return std::isxdigit(c) || c == ' ';
            });

        // If the string starts with optional hex digits followed by a colon
        // it's an IVv6 address.  We're done.
        if (colon == ret.end() || (*colon) == ':')
            return ret;
    }

    // If there's a port appended to the IP address, strip that by
    // terminating at the colon.
    if (std::size_t colon = ret.find(':'); colon != std::string_view::npos)
        ret = ret.substr(0, colon);

    return ret;
}

std::string_view
forwardedFor(http_request_type const& request)
{
    // Look for the Forwarded field in the request.
    if (auto it = request.find(boost::beast::http::field::forwarded);
        it != request.end())
    {
        auto ascii_tolower = [](char c) -> char {
            return ((static_cast<unsigned>(c) - 65U) < 26) ? c + 'a' - 'A' : c;
        };

        // Look for the first (case insensitive) "for="
        static std::string const forStr{"for="};
        char const* found = std::search(
            it->value().begin(),
            it->value().end(),
            forStr.begin(),
            forStr.end(),
            [&ascii_tolower](char c1, char c2) {
                return ascii_tolower(c1) == ascii_tolower(c2);
            });

        if (found == it->value().end())
            return {};

        found += forStr.size();

        // We found a "for=".  Scan for the end of the IP address.
        std::size_t const pos = [&found, &it]() {
            std::size_t pos = std::string_view(found, it->value().end() - found)
                                  .find_first_of(",;");
            if (pos != std::string_view::npos)
                return pos;

            return it->value().size() - forStr.size();
        }();

        return extractIpAddrFromField({found, pos});
    }

    // Look for the X-Forwarded-For field in the request.
    if (auto it = request.find("X-Forwarded-For"); it != request.end())
    {
        // The first X-Forwarded-For entry may be terminated by a comma.
        std::size_t found = it->value().find(',');
        if (found == boost::string_view::npos)
            found = it->value().length();
        return extractIpAddrFromField(it->value().substr(0, found));
    }

    return {};
}

}  // namespace ripple
