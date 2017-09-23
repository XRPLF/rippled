//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_EXAMPLE_SERVER_WS_UPGRADE_SERVICE_HPP
#define BEAST_EXAMPLE_SERVER_WS_UPGRADE_SERVICE_HPP

#include "framework.hpp"

#include <beast/http/message.hpp>
#include <beast/websocket/rfc6455.hpp>
#include <memory>

namespace framework {

/** An HTTP service which transfers WebSocket upgrade request to another port handler.

    @tparam PortHandler The type of port handler. The service will
    handle WebSocket Upgrade requests by transferring ownership
    of the stream and request to a port handler of this type.
*/
template<class PortHandler>
class ws_upgrade_service
{
    PortHandler& handler_;

public:
    /** Constructor

        @param handler A shared pointer to the @b PortHandler to
        handle WebSocket upgrade requests.
    */
    explicit
    ws_upgrade_service(PortHandler& handler)
        : handler_(handler)
    {
    }

    /** Initialize the service.

        This provides an opportunity for the service to perform
        initialization which may fail, while reporting an error
        code instead of throwing an exception from the constructor.
    */
    void
    init(error_code& ec)
    {
        // This is required by the error_code specification
        //
        ec = {};
    }

    /** Handle a WebSocket Upgrade request.

        If the request is an upgrade request, ownership of the
        stream and request will be transferred to the corresponding
        WebSocket port handler.

        @param stream The stream corresponding to the connection.

        @param ep The remote endpoint associated with the stream.

        @req The request to check.
    */
    template<
        class Stream,
        class Body,
        class Send>
    bool
    respond(
        Stream&& stream,
        endpoint_type const& ep,
        beast::http::request<Body>&& req,
        Send const&) const
    {
        // If its not an upgrade request, return `false`
        // to indicate that we are not handling it.
        //
        if(! beast::websocket::is_upgrade(req))
            return false;

        // Its an ugprade request, so transfer ownership
        // of the stream and request to the port handler.
        //
        handler_.on_upgrade(
            std::move(stream),
            ep,
            std::move(req));

        // Tell the service list that we handled the request.
        //
        return true;
    }
};

} // framework

#endif
