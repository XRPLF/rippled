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

#ifndef RIPPLE_HTTP_SERVER_H_INCLUDED
#define RIPPLE_HTTP_SERVER_H_INCLUDED

#include <ripple/basics/BasicConfig.h>
#include <ripple/http/Writer.h>
#include <beast/asio/ssl_bundle.h>
#include <beast/http/message.h>
#include <beast/net/IPEndpoint.h>
#include <beast/utility/ci_char_traits.h>
#include <beast/utility/Journal.h>
#include <beast/utility/PropertyStream.h>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/system/error_code.hpp>
#include <cstdint>
#include <memory>
#include <ostream>
#include <set>

namespace ripple {
namespace HTTP {

//------------------------------------------------------------------------------

/** Configuration information for a server listening port. */
struct Port
{
    std::string name;
    boost::asio::ip::address ip;
    std::uint16_t port = 0;
    std::set<std::string, beast::ci_less> protocol;
    bool allow_admin = false;
    std::string user;
    std::string password;
    std::string admin_user;
    std::string admin_password;
    std::string ssl_key;
    std::string ssl_cert;
    std::string ssl_chain;
    std::shared_ptr<boost::asio::ssl::context> context;

    // Returns `true` if any websocket protocols are specified
    bool
    websockets() const;

    // Returns a string containing the list of protocols
    std::string
    protocols() const;
};

//------------------------------------------------------------------------------

class Server;
class Session;

/** Processes all sessions.
    Thread safety:
        Must be safe to call concurrently from any number of foreign threads.
*/
struct Handler
{
    /** Called when the connection is accepted and we know remoteAddress. */
    // DEPRECATED
    virtual void onAccept (Session& session) = 0;

    /** Called when a connection is accepted.
        @return `true` If we should keep the connection.
    */
    virtual
    bool
    onAccept (Session& session,
        boost::asio::ip::tcp::endpoint remote_address) = 0;

    /** Called when a legacy peer protocol handshake is detected.
        If the called function does not take ownership, then the
        connection is closed.
        @param buffer The unconsumed bytes in the protocol handshake
        @param ssl_bundle The active connection.
    */
    virtual
    void
    onLegacyPeerHello (std::unique_ptr<beast::asio::ssl_bundle>&& ssl_bundle,
        boost::asio::const_buffer buffer,
            boost::asio::ip::tcp::endpoint remote_address) = 0;

    struct What
    {
        // When `true`, the Session will close the socket. The
        // Handler may optionally take socket ownership using std::move
        bool moved = false;

        // If response is set, this determines the keep alive
        bool keep_alive = false;

        // When set, this will be sent back
        std::shared_ptr<Writer> response;

        bool handled() const
        {
            return moved || response;
        }
    };

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
    What
    onMaybeMove (Session& session,
        std::unique_ptr <beast::asio::ssl_bundle>&& bundle,
            beast::http::message&& request,
                boost::asio::ip::tcp::endpoint remote_address) = 0;

    virtual
    What
    onMaybeMove (Session& session, boost::asio::ip::tcp::socket&& socket,
        beast::http::message&& request,
            boost::asio::ip::tcp::endpoint remote_address) = 0;
    /** @} */

    /** Called when we have a complete HTTP request. */
    // VFALCO TODO Pass the beast::http::message as a parameter
    virtual void onRequest (Session& session) = 0;

    /** Called when the session ends.
        Guaranteed to be called once.
        @param errorCode Non zero for a failed connection.
    */
    virtual void onClose (Session& session,
        boost::system::error_code const& ec) = 0;

    /** Called when the server has finished its stop. */
    virtual void onStopped (Server& server) = 0;
};

//------------------------------------------------------------------------------

/** Multi-threaded, asynchronous HTTP server. */
class Server
{
public:
    /** Destroy the server.
        The server is closed if it is not already closed. This call
        blocks until the server has stopped.
    */
    virtual
    ~Server() = default;

    /** Returns the Journal associated with the server. */
    virtual
    beast::Journal
    journal() = 0;

    /** Set the listening port settings.
        This may only be called once.
    */
    virtual
    void
    ports (std::vector<Port> const& v) = 0;

    virtual
    void
    onWrite (beast::PropertyStream::Map& map) = 0;

    /** Close the server.
        The close is performed asynchronously. The handler will be notified
        when the server has stopped. The server is considered stopped when
        there are no pending I/O completion handlers and all connections
        have closed.
        Thread safety:
            Safe to call concurrently from any thread.
    */
    virtual
    void
    close() = 0;

    /** Parse configuration settings into a list of ports. */
    static
    std::vector<Port>
    parse (BasicConfig const& config, std::ostream& log);
};

//------------------------------------------------------------------------------

/** Create the HTTP server using the specified handler. */
std::unique_ptr<Server>
make_Server (Handler& handler,
    boost::asio::io_service& io_service, beast::Journal journal);

} // HTTP
} // ripple

#endif
