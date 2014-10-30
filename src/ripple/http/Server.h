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
#include <ripple/common/RippleSSLContext.h>
#include <beast/net/IPEndpoint.h>
#include <beast/utility/Journal.h>
#include <beast/utility/PropertyStream.h>
#include <boost/system/error_code.hpp>
#include <cstdint>
#include <memory>
#include <ostream>

namespace ripple {
namespace HTTP {

//------------------------------------------------------------------------------

/** Configuration information for a server listening port. */
struct Port
{
    enum class Security
    {
        no_ssl,
        allow_ssl,
        require_ssl
    };

    Security security = Security::no_ssl;
    std::uint16_t port = 0;
    beast::IP::Endpoint addr;
    SSLContext* context = nullptr;

    Port() = default;
    Port (std::uint16_t port_, beast::IP::Endpoint const& addr_,
        Security security_, SSLContext* context_);

    static
    void
    parse (Port& result, Section const& section, std::ostream& log);
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
    virtual void onAccept (Session& session) = 0;

    /** Called when we have a complete HTTP request. */
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

/** Create the HTTP server using the specified handler. */
std::unique_ptr<Server>
make_Server (Handler& handler, beast::Journal journal);

//------------------------------------------------------------------------------

} // HTTP
} // ripple

#endif
