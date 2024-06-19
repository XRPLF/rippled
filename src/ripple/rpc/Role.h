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

#ifndef RIPPLE_SERVER_ROLE_H_INCLUDED
#define RIPPLE_SERVER_ROLE_H_INCLUDED

#include <ripple/beast/net/IPEndpoint.h>
#include <ripple/json/json_value.h>
#include <ripple/resource/ResourceManager.h>
#include <ripple/server/Handoff.h>
#include <ripple/server/Port.h>
#include <boost/asio/ip/network_v4.hpp>
#include <boost/asio/ip/network_v6.hpp>
#include <boost/utility/string_view.hpp>
#include <string>
#include <variant>
#include <vector>

namespace ripple {

/** Indicates the level of administrative permission to grant.
 * IDENTIFIED role has unlimited resources but cannot perform some
 *            RPC commands.
 * ADMIN role has unlimited resources and is able to perform all RPC
 *            commands.
 */
enum class Role { GUEST, USER, IDENTIFIED, ADMIN, PROXY, FORBID };

/** Return the allowed privilege role.
    params must meet the requirements of the JSON-RPC
    specification. It must be of type Object, containing the key params
    which is an array with at least one object. Inside this object
    are the optional keys 'admin_user' and 'admin_password' used to
    validate the credentials. If user is non-blank, it's username
    passed in the HTTP header by a secure_gateway proxy.
*/
Role
requestRole(
    Role const& required,
    Port const& port,
    Json::Value const& params,
    beast::IP::Endpoint const& remoteIp,
    std::string_view user);

Resource::Consumer
requestInboundEndpoint(
    Resource::Manager& manager,
    beast::IP::Endpoint const& remoteAddress,
    Role const& role,
    std::string_view user,
    std::string_view forwardedFor);

/**
 * Check if the role entitles the user to unlimited resources.
 */
bool
isUnlimited(Role const& role);

/**
 * True if remoteIp is in any of adminIp
 *
 * @param remoteIp Remote address for which to search.
 * @param adminIp  List of IP's in which to search.
 * @return Whether remoteIp is in adminIp.
 */
bool
ipAllowed(
    beast::IP::Address const& remoteIp,
    std::vector<boost::asio::ip::network_v4> const& nets4,
    std::vector<boost::asio::ip::network_v6> const& nets6);

std::string_view
forwardedFor(http_request_type const& request);

}  // namespace ripple

#endif
