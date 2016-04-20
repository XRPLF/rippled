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

#ifndef RIPPLE_SERVER_HANDLER_H_INCLUDED
#define RIPPLE_SERVER_HANDLER_H_INCLUDED

#include <ripple/server/Handoff.h>
#include <ripple/server/WSSession.h>
#include <ripple/beast/asio/ssl_bundle.h>
#include <boost/asio/buffer.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/system/error_code.hpp>
#include <memory>

namespace ripple {

class Session;
class Server;

/** Processes all sessions.
    Thread safety:
        Must be safe to call concurrently from any number of foreign threads.
*/
struct Handler
{
    /** Called when a connection is accepted.
        @return `true` If we should keep the connection.
    */
    virtual
    bool
    onAccept (Session& session,
        boost::asio::ip::tcp::endpoint remote_address) = 0;

    /** Called to process a complete HTTP request.
        The handler can do one of three things:
            - Ignore the request (return default constructed What)
            - Return a response (by setting response in the What)
            - Take ownership of the socket by using rvalue move
              and setting moved = `true` in the What.
        If the handler ignores the request, the legacy onRequest
        is called.
    */
    /** @{ */
    virtual
    Handoff
    onHandoff (Session& session,
        std::unique_ptr <beast::asio::ssl_bundle>&& bundle,
            http_request_type&& request,
                boost::asio::ip::tcp::endpoint remote_address) = 0;

    virtual
    Handoff
    onHandoff (Session& session, boost::asio::ip::tcp::socket&& socket,
        http_request_type&& request,
            boost::asio::ip::tcp::endpoint remote_address) = 0;
    /** @} */

    /** Called when we have a complete HTTP request. */
    // VFALCO TODO Pass the beast::deprecated_http::message as a parameter
    virtual void onRequest (Session& session) = 0;

    /** Called when the session ends.
        Guaranteed to be called once.
        @param errorCode Non zero for a failed connection.
    */
    virtual void onClose (Session& session,
        boost::system::error_code const& ec) = 0;

    /** Called when the server has finished its stop. */
    virtual void onStopped (Server& server) = 0;

    //
    // WebSockets
    //

    /** Called on a WebSocket Upgrade request. */


    /** Called for each complete WebSocket message. */
    virtual
    void
    onWSMessage(std::shared_ptr<WSSession> session,
        std::vector<boost::asio::const_buffer> const& buffers) = 0;

};

} // ripple

#endif
