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

#ifndef RIPPLE_APP_WEBSOCKET_WSCONNECTION_H_INCLUDED
#define RIPPLE_APP_WEBSOCKET_WSCONNECTION_H_INCLUDED

#include <ripple/app/main/Application.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/basics/CountedObject.h>
#include <ripple/basics/Log.h>
#include <ripple/json/to_string.h>
#include <ripple/net/InfoSub.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/resource/Fees.h>
#include <ripple/resource/ResourceManager.h>
#include <ripple/rpc/Coroutine.h>
#include <ripple/rpc/RPCHandler.h>
#include <ripple/server/Port.h>
#include <ripple/json/to_string.h>
#include <ripple/rpc/RPCHandler.h>
#include <ripple/rpc/Yield.h>
#include <ripple/server/Role.h>
#include <ripple/websocket/WebSocket.h>

#include <boost/asio.hpp>
#include <beast/asio/placeholders.h>
#include <memory>

namespace ripple {
namespace websocket {

template <class WebSocket>
class HandlerImpl;

/** A Ripple WebSocket connection handler.
*/
template <class WebSocket>
class ConnectionImpl
    : public std::enable_shared_from_this <ConnectionImpl <WebSocket> >
    , public InfoSub
    , public CountedObject <ConnectionImpl <WebSocket>>
{
public:
    static char const* getCountedObjectName () { return "ConnectionImpl"; }

    using message_ptr = typename WebSocket::MessagePtr;
    using connection = typename WebSocket::Connection;
    using connection_ptr = typename WebSocket::ConnectionPtr;
    using weak_connection_ptr = typename WebSocket::ConnectionWeakPtr;
    using handler_type = HandlerImpl <WebSocket>;

    ConnectionImpl (
        Application& app,
        Resource::Manager& resourceManager,
        InfoSub::Source& source,
        handler_type& handler,
        connection_ptr const& cpConnection,
        beast::IP::Endpoint const& remoteAddress,
        boost::asio::io_service& io_service);

    void preDestroy ();

    static void destroy (std::shared_ptr <ConnectionImpl <WebSocket> >)
    {
        // Just discards the reference
    }

    void send (Json::Value const& jvObj, bool broadcast);

    void disconnect ();
    static void handle_disconnect(weak_connection_ptr c);

    bool onPingTimer (std::string&);
    void pingTimer (typename WebSocket::ErrorCode const& e);

    void onPong (std::string const&);
    void rcvMessage (message_ptr const&, bool& msgRejected, bool& runQueue);
    message_ptr getMessage ();
    bool checkMessage ();
    void returnMessage (message_ptr const&);
    Json::Value invokeCommand (Json::Value const& jvRequest, RPC::Suspend const&);

    // Generically implemented per version.
    void setPingTimer ();

private:
    Application& app_;
    HTTP::Port const& m_port;
    Resource::Manager& m_resourceManager;
    Resource::Consumer m_usage;
    beast::IP::Endpoint const m_remoteAddress;
    std::mutex m_receiveQueueMutex;
    std::deque <message_ptr> m_receiveQueue;
    NetworkOPs& m_netOPs;
    boost::asio::io_service& m_io_service;
    boost::asio::deadline_timer m_pingTimer;

    bool m_sentPing = false;
    bool m_receiveQueueRunning = false;
    bool m_isDead = false;

    handler_type& m_handler;
    weak_connection_ptr m_connection;
};

template <class WebSocket>
ConnectionImpl <WebSocket>::ConnectionImpl (
    Application& app,
    Resource::Manager& resourceManager,
    InfoSub::Source& source,
    handler_type& handler,
    connection_ptr const& cpConnection,
    beast::IP::Endpoint const& remoteAddress,
    boost::asio::io_service& io_service)
        : InfoSub (source, // usage
                   resourceManager.newInboundEndpoint (remoteAddress))
        , app_(app)
        , m_port (handler.port ())
        , m_resourceManager (resourceManager)
        , m_remoteAddress (remoteAddress)
        , m_netOPs (app_.getOPs ())
        , m_io_service (io_service)
        , m_pingTimer (io_service)
        , m_handler (handler)
        , m_connection (cpConnection)
{
}

template <class WebSocket>
void ConnectionImpl <WebSocket>::onPong (std::string const&)
{
    m_sentPing = false;
}

template <class WebSocket>
void ConnectionImpl <WebSocket>::rcvMessage (
    message_ptr const& msg, bool& msgRejected, bool& runQueue)
{
    WriteLog (lsWARNING, ConnectionImpl)
            << "WebSocket: rcvMessage";
    ScopedLockType sl (m_receiveQueueMutex);

    if (m_isDead)
    {
        msgRejected = false;
        runQueue = false;
        return;
    }

    if ((m_receiveQueue.size () >= 1000) ||
        (msg->get_payload().size() > 1000000))
    {
        msgRejected = true;
        runQueue = false;
    }
    else
    {
        msgRejected = false;
        m_receiveQueue.push_back (msg);

        if (m_receiveQueueRunning)
            runQueue = false;
        else
        {
            runQueue = true;
            m_receiveQueueRunning = true;
        }
    }
}

template <class WebSocket>
bool ConnectionImpl <WebSocket>::checkMessage ()
{
    ScopedLockType sl (m_receiveQueueMutex);

    assert (m_receiveQueueRunning);

    if (m_isDead || m_receiveQueue.empty ())
    {
        m_receiveQueueRunning = false;
        return false;
    }

    return true;
}

template <class WebSocket>
typename WebSocket::MessagePtr ConnectionImpl <WebSocket>::getMessage ()
{
    ScopedLockType sl (m_receiveQueueMutex);

    if (m_isDead || m_receiveQueue.empty ())
    {
        m_receiveQueueRunning = false;
        return message_ptr ();
    }

    message_ptr m = m_receiveQueue.front ();
    m_receiveQueue.pop_front ();
    return m;
}

template <class WebSocket>
void ConnectionImpl <WebSocket>::returnMessage (message_ptr const& ptr)
{
    ScopedLockType sl (m_receiveQueueMutex);

    if (!m_isDead)
    {
        m_receiveQueue.push_front (ptr);
        m_receiveQueueRunning = false;
    }
}

template <class WebSocket>
Json::Value ConnectionImpl <WebSocket>::invokeCommand (
    Json::Value const& jvRequest, RPC::Suspend const& suspend)
{
    if (getConsumer().disconnect ())
    {
        disconnect ();
        return rpcError (rpcSLOW_DOWN);
    }

    // Requests without "command" are invalid.
    //
    if (!jvRequest.isMember (jss::command))
    {
        Json::Value jvResult (Json::objectValue);

        jvResult[jss::type]    = jss::response;
        jvResult[jss::status]  = jss::error;
        jvResult[jss::error]   = jss::missingCommand;
        jvResult[jss::request] = jvRequest;

        if (jvRequest.isMember (jss::id))
        {
            jvResult[jss::id]  = jvRequest[jss::id];
        }

        getConsumer().charge (Resource::feeInvalidRPC);

        return jvResult;
    }

    Resource::Charge loadType = Resource::feeReferenceRPC;
    Json::Value jvResult (Json::objectValue);

    auto required = RPC::roleRequired (jvRequest[jss::command].asString());
    auto role = requestRole (required, m_port, jvRequest, m_remoteAddress);

    if (Role::FORBID == role)
    {
        jvResult[jss::result]  = rpcError (rpcFORBIDDEN);
    }
    else
    {
        RPC::Context context {
            jvRequest, app_, loadType, m_netOPs, app_.getLedgerMaster(),
            role, {app_, suspend, "WSClient::command"},
            this->shared_from_this ()};
        RPC::doCommand (context, jvResult[jss::result]);
    }

    getConsumer().charge (loadType);
    if (getConsumer().warn ())
    {
        jvResult[jss::warning] = jss::load;
    }

    // Currently we will simply unwrap errors returned by the RPC
    // API, in the future maybe we can make the responses
    // consistent.
    //
    // Regularize result. This is duplicate code.
    if (jvResult[jss::result].isMember (jss::error))
    {
        jvResult               = jvResult[jss::result];
        jvResult[jss::status]  = jss::error;
        jvResult[jss::request] = jvRequest;

    }
    else
    {
        jvResult[jss::status] = jss::success;
    }

    if (jvRequest.isMember (jss::id))
    {
        jvResult[jss::id] = jvRequest[jss::id];
    }

    jvResult[jss::type] = jss::response;

    return jvResult;
}

template <class WebSocket>
void ConnectionImpl <WebSocket>::preDestroy ()
{
    // sever connection
    this->m_pingTimer.cancel ();
    m_connection.reset ();

    {
        ScopedLockType sl (this->m_receiveQueueMutex);
        this->m_isDead = true;
    }
}

// Implement overridden functions from base class:
template <class WebSocket>
void ConnectionImpl <WebSocket>::send (Json::Value const& jvObj, bool broadcast)
{
    WriteLog (lsWARNING, ConnectionImpl)
            << "WebSocket: sending '" << to_string (jvObj);
    connection_ptr ptr = m_connection.lock ();

    if (ptr)
        m_handler.send (ptr, jvObj, broadcast);
}

template <class WebSocket>
void ConnectionImpl <WebSocket>::disconnect ()
{
    WriteLog (lsWARNING, ConnectionImpl)
            << "WebSocket: disconnecting";
    connection_ptr ptr = m_connection.lock ();

    if (ptr)
        this->m_io_service.dispatch (
            WebSocket::getStrand (*ptr).wrap (
                std::bind(&ConnectionImpl <WebSocket>::handle_disconnect,
                          m_connection)));
}

// static
template <class WebSocket>
void ConnectionImpl <WebSocket>::handle_disconnect(weak_connection_ptr c)
{
    connection_ptr ptr = c.lock ();

    if (ptr)
        WebSocket::handleDisconnect (*ptr);
}

template <class WebSocket>
bool ConnectionImpl <WebSocket>::onPingTimer (std::string&)
{
    if (this->m_sentPing)
        return true; // causes connection to close

    this->m_sentPing = true;
    setPingTimer ();
    return false; // causes ping to be sent
}

//--------------------------------------------------------------------------

template <class WebSocket>
void ConnectionImpl <WebSocket>::pingTimer (
    typename WebSocket::ErrorCode const& e)
{
    if (!e)
    {
        if (auto ptr = this->m_connection.lock ())
            this->m_handler.pingTimer (ptr);
    }
}

} // websocket
} // ripple

#endif
