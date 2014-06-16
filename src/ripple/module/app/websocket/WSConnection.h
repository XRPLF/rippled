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

#ifndef RIPPLE_WSCONNECTION_H
#define RIPPLE_WSCONNECTION_H

#include <beast/asio/placeholders.h>

namespace ripple {

/** A Ripple WebSocket connection handler.
    This handles everything that is independent of the endpint_type.
*/
class WSConnection
    : public std::enable_shared_from_this <WSConnection>
    , public InfoSub
    , public CountedObject <WSConnection>
    , public beast::Uncopyable
{
public:
    static char const* getCountedObjectName () { return "WSConnection"; }

protected:
    typedef websocketpp::message::data::ptr message_ptr;

    WSConnection (Resource::Manager& resourceManager,
        Resource::Consumer usage, InfoSub::Source& source, bool isPublic,
            beast::IP::Endpoint const& remoteAddress, boost::asio::io_service& io_service);

    virtual ~WSConnection ();

    virtual void preDestroy () = 0;
    virtual void disconnect () = 0;

public:
    void onPong (const std::string&);
    void rcvMessage (message_ptr msg, bool& msgRejected, bool& runQueue);
    message_ptr getMessage ();
    bool checkMessage ();
    void returnMessage (message_ptr ptr);
    Json::Value invokeCommand (Json::Value& jvRequest);

protected:
    Resource::Manager& m_resourceManager;
    Resource::Consumer m_usage;
    bool const m_isPublic;
    beast::IP::Endpoint const m_remoteAddress;
    LockType m_receiveQueueMutex;
    std::deque <message_ptr> m_receiveQueue;
    NetworkOPs& m_netOPs;
    boost::asio::deadline_timer m_pingTimer;
    bool m_sentPing;
    bool m_receiveQueueRunning;
    bool m_isDead;
    boost::asio::io_service& m_io_service;

private:
    WSConnection (WSConnection const&);
    WSConnection& operator= (WSConnection const&);
};

//------------------------------------------------------------------------------

template <typename endpoint_type>
class WSServerHandler;

/** A Ripple WebSocket connection handler for a specific endpoint_type.
*/
template <typename endpoint_type>
class WSConnectionType
    : public WSConnection
{
public:
    typedef typename endpoint_type::connection_type connection;
    typedef typename boost::shared_ptr<connection> connection_ptr;
    typedef typename boost::weak_ptr<connection> weak_connection_ptr;
    typedef WSServerHandler <endpoint_type> server_type;

public:
    WSConnectionType (Resource::Manager& resourceManager,
        InfoSub::Source& source, server_type& serverHandler,
            connection_ptr const& cpConnection)
        : WSConnection (
            resourceManager,
            resourceManager.newInboundEndpoint (cpConnection->get_socket ().remote_endpoint ()),
            source,
            serverHandler.getPublic (),
            cpConnection->get_socket ().remote_endpoint (),
            cpConnection->get_io_service ())
        , m_serverHandler (serverHandler)
        , m_connection (cpConnection)
    {
        setPingTimer ();
    }

    void preDestroy ()
    {
        // sever connection
        m_pingTimer.cancel ();
        m_connection.reset ();

        {
            ScopedLockType sl (m_receiveQueueMutex);
            m_isDead = true;
        }
    }

    static void destroy (std::shared_ptr <WSConnectionType <endpoint_type> >)
    {
        // Just discards the reference
    }

    // Implement overridden functions from base class:
    void send (const Json::Value& jvObj, bool broadcast)
    {
        connection_ptr ptr = m_connection.lock ();

        if (ptr)
            m_serverHandler.send (ptr, jvObj, broadcast);
    }

    void send (const Json::Value& jvObj, const std::string& sObj, bool broadcast)
    {
        connection_ptr ptr = m_connection.lock ();

        if (ptr)
            m_serverHandler.send (ptr, sObj, broadcast);
    }

    void disconnect ()
    {
        connection_ptr ptr = m_connection.lock ();

        if (ptr)
            m_io_service.dispatch (ptr->get_strand ().wrap (std::bind (
                &WSConnectionType <endpoint_type>::handle_disconnect,
                    m_connection)));
    }

    static void handle_disconnect(weak_connection_ptr c)
    {
        connection_ptr ptr = c.lock ();

        if (ptr)
            ptr->close (websocketpp::close::status::PROTOCOL_ERROR, "overload");
    }

    bool onPingTimer (std::string&)
    {
        if (m_sentPing)
            return true; // causes connection to close

        m_sentPing = true;
        setPingTimer ();
        return false; // causes ping to be sent
    }

    //--------------------------------------------------------------------------

    static void pingTimer (weak_connection_ptr c, server_type* h,
        boost::system::error_code const& e)
    {
        if (e)
            return;

        connection_ptr ptr = c.lock ();

        if (ptr)
            h->pingTimer (ptr);
    }

    void setPingTimer ()
    {
        connection_ptr ptr = m_connection.lock ();

        if (ptr)
        {
            m_pingTimer.expires_from_now (boost::posix_time::seconds 
                (getConfig ().WEBSOCKET_PING_FREQ));

            m_pingTimer.async_wait (ptr->get_strand ().wrap (
                std::bind (&WSConnectionType <endpoint_type>::pingTimer,
                    m_connection, &m_serverHandler, beast::asio::placeholders::error)));
        }
    }

private:
    server_type& m_serverHandler;
    weak_connection_ptr m_connection;
};

} // ripple

#endif
