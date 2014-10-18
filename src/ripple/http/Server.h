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

#include <beast/asio/ssl_bundle.h>
#include <beast/http/message.h>
#include <beast/net/IPEndpoint.h>
#include <beast/module/asio/basics/SSLContext.h>
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
    beast::asio::SSLContext* context = nullptr;

    Port() = default;
    Port (std::uint16_t port_, beast::IP::Endpoint const& addr_,
            Security security_, beast::asio::SSLContext* context_);
};

bool operator== (Port const& lhs, Port const& rhs);
bool operator<  (Port const& lhs, Port const& rhs);

/** A set of listening ports settings. */
typedef std::vector <Port> Ports;

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

    //
    // ---
    //

    /** Called when a connection is accepted.
        @return `true` If we should keep the connection.
    */
    virtual
    bool
    accept (boost::asio::ip::tcp::endpoint endpoint)
    {
        return true;
    }

    enum class Result
    {
        none,
        move,
        response
    };

    /** Called when a legacy peer protocol handshake is detected.
        If the called function does not take ownership, then the
        connection is closed.
        @param buffer The unconsumed bytes in the protocol handshake
        @param ssl_bundle The active connection.
    */
    virtual
    void
    on_legacy_peer_handshake (boost::asio::const_buffer buffer,
        boost::asio::ip::tcp::endpoint remote_address,
            std::unique_ptr<beast::asio::ssl_bundle>&& ssl_bundle) = 0;

    /** Called to process a complete HTTP request.
        Outcomes:
            - Does not want the request
            - Provides a message response
            - Takes over the socket
    */
    /** @{ */
    virtual
    Result
    process (std::unique_ptr <beast::asio::ssl_bundle>& bundle,
        boost::asio::ip::tcp::endpoint endpoint,
            beast::http::message& request,
                beast::http::message& response)
    {
        return Result::none;
    }

    virtual
    Result
    process (boost::asio::ip::tcp::socket& socket,
        boost::asio::ip::tcp::endpoint endpoint,
            beast::http::message& request,
                beast::http::message& response)
    {
        return Result::none;
    }
    /** @} */
};

//------------------------------------------------------------------------------

class ServerImpl;

/** Multi-threaded, asynchronous HTTP server. */
class Server
{
public:
    /** Create the server using the specified handler. */
    Server (Handler& handler, beast::Journal journal);

    /** Destroy the server.
        This blocks until the server stops.
    */
    virtual
    ~Server ();

    /** Returns the Journal associated with the server. */
    beast::Journal
    journal () const;

    /** Returns the listening ports settings.
        Thread safety:
            Safe to call from any thread.
            Cannot be called concurrently with setPorts.
    */
    Ports const&
    getPorts () const;

    /** Set the listening ports settings.
        These take effect immediately. Any current ports that are not in the
        new set will be closed. Established connections will not be disturbed.
        Thread safety:
            Cannot be called concurrently.
    */
    void
    setPorts (Ports const& ports);

    /** Notify the server to stop, without blocking.
        Thread safety:
            Safe to call concurrently from any thread.
    */
    void
    stopAsync ();

    /** Notify the server to stop, and block until the stop is complete.
        The handler's onStopped method will be called when the stop completes.
        Thread safety:
            Cannot be called concurrently.
            Cannot be called from the thread of execution of any Handler functions.
    */
    void
    stop ();

    void
    onWrite (beast::PropertyStream::Map& map);

private:
    std::unique_ptr <ServerImpl> m_impl;
};

} // HTTP
} // ripple

#endif
