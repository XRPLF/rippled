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

#ifndef RIPPLE_APP_MAIN_SERVERHANDLER_H_INCLUDED
#define RIPPLE_APP_MAIN_SERVERHANDLER_H_INCLUDED

#include <ripple/core/Config.h>
#include <ripple/http/Server.h>
#include <ripple/overlay/Overlay.h>
#include <beast/utility/Journal.h>
#include <beast/utility/PropertyStream.h>
#include <beast/cxx14/memory.h> // <memory>
#include <boost/asio/ip/address.hpp>
#include <vector>

namespace ripple {

class ServerHandler
    : public beast::Stoppable
    , public beast::PropertyStream::Source
{
protected:
    ServerHandler (Stoppable& parent);

public:
    struct Setup
    {
        std::vector<HTTP::Port> ports;

        // Memberspace
        struct client_t
        {
            bool secure;
            std::string ip;
            std::uint16_t port;
            std::string user;
            std::string password;
            std::string admin_user;
            std::string admin_password;
        };

        // Configuration when acting in client role
        client_t client;

        // Configuration for the Overlay
        struct overlay_t
        {
            boost::asio::ip::address ip;
            std::uint16_t port = 0;
        };

        overlay_t overlay;
    };

    virtual
    ~ServerHandler() = default;

    /** Opens listening ports based on the Config settings
        This is implemented outside the constructor to support
        two-stage initialization in the Application object.
    */
    virtual
    void
    setup (Setup const& setup, beast::Journal journal) = 0;

    /** Returns the setup associated with the handler. */
    virtual
    Setup const&
    setup() const = 0;
};

//------------------------------------------------------------------------------

/** Determine the level of administrative permission to grant. */
/** @{ */
enum class Role
{
    GUEST,
    USER,
    ADMIN,
    FORBID
};

/** Return the allowed privilege role.
    jsonRPC must meet the requirements of the JSON-RPC
    specification. It must be of type Object, containing the key params
    which is an array with at least one object. Inside this object
    are the optional keys 'admin_user' and 'admin_password' used to
    validate the credentials.
*/
Role
adminRole (HTTP::Port const& port, Json::Value const& jsonRPC,
    beast::IP::Endpoint const& remoteIp);
/** @} */

ServerHandler::Setup
setup_ServerHandler (BasicConfig const& c, std::ostream& log);

} // ripple

#endif
