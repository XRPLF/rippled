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
#include <ripple/core/JobQueue.h>
#include <ripple/json/to_string.h>
#include <ripple/net/InfoSub.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/resource/Fees.h>
#include <ripple/resource/ResourceManager.h>
#include <ripple/rpc/RPCHandler.h>
#include <ripple/server/Port.h>
#include <ripple/rpc/Role.h>
#include <ripple/json/to_string.h>
#include <ripple/rpc/RPCHandler.h>
#include <ripple/rpc/Role.h>

#include <boost/asio.hpp>
#include <beast/core/placeholders.hpp>
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
        boost::asio::io_service& io_service,
        std::pair<std::string, std::string> identity);

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
    boost::optional <std::string>  getMessage ();
    bool checkMessage ();
    Json::Value invokeCommand (Json::Value const& jvRequest,
        std::shared_ptr<JobQueue::Coro> coro);

    // Generically implemented per version.
    void setPingTimer ();

private:
    Application& app_;
    Port const& m_port;
    Resource::Manager& m_resourceManager;
    beast::IP::Endpoint const m_remoteAddress;
    std::string const m_forwardedFor;
    std::string const m_user;
    std::mutex m_receiveQueueMutex;
    std::deque <std::string> m_receiveQueue;
    NetworkOPs& m_netOPs;
    boost::asio::io_service& m_io_service;
    boost::asio::basic_waitable_timer<std::chrono::system_clock> m_pingTimer;

    bool m_sentPing = false;
    bool m_receiveQueueRunning = false;
    bool m_isDead = false;

    handler_type& m_handler;
    weak_connection_ptr m_connection;

    std::chrono::seconds pingFreq_;
    beast::Journal j_;
};

template <class WebSocket>
ConnectionImpl <WebSocket>::ConnectionImpl (
    Application& app,
    Resource::Manager& resourceManager,
    InfoSub::Source& source,
    handler_type& handler,
    connection_ptr const& cpConnection,
    beast::IP::Endpoint const& remoteAddress,
    boost::asio::io_service& io_service,
    std::pair<std::string, std::string> identity)
        : InfoSub (source, requestInboundEndpoint (
            resourceManager, remoteAddress, handler.port(), identity.second))
        , app_(app)
        , m_port (handler.port ())
        , m_resourceManager (resourceManager)
        , m_remoteAddress (remoteAddress)
        , m_forwardedFor (isIdentified (m_port, m_remoteAddress.address(),
            identity.second) ? identity.first : std::string())
        , m_user (isIdentified (m_port, m_remoteAddress.address(),
            identity.second) ? identity.second : std::string())
        , m_netOPs (app_.getOPs ())
        , m_io_service (io_service)
        , m_pingTimer (io_service)
        , m_handler (handler)
        , m_connection (cpConnection)
        , pingFreq_ (app.config ().WEBSOCKET_PING_FREQ)
        , j_ (app.journal ("ConnectionImpl"))
{
    // VFALCO Disabled since it might cause hangs
    pingFreq_ = std::chrono::seconds{0};

    if (! m_forwardedFor.empty() || ! m_user.empty())
    {
        JLOG(j_.debug()) << "connect secure_gateway X-Forwarded-For: " <<
            m_forwardedFor << ", X-User: " << m_user;
    }
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
    JLOG(j_.debug()) <<
        "WebSocket: received " << msg->get_payload();

    ScopedLockType sl (m_receiveQueueMutex);

    if (m_isDead)
    {
        msgRejected = false;
        runQueue = false;
        return;
    }

    if ((m_receiveQueue.size () >= 1000) ||
        (msg->get_payload().size() > 1000000) ||
        ! WebSocket::isTextMessage (*msg))
    {
        msgRejected = true;
        runQueue = false;
    }
    else
    {
        msgRejected = false;
        m_receiveQueue.push_back (msg->get_payload ());

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
boost::optional <std::string>
ConnectionImpl <WebSocket>::getMessage ()
{
    ScopedLockType sl (m_receiveQueueMutex);

    if (m_isDead || m_receiveQueue.empty ())
    {
        m_receiveQueueRunning = false;
        return boost::none;
    }

    boost::optional <std::string> ret (std::move (m_receiveQueue.front ()));
    m_receiveQueue.pop_front ();
    return ret;
}

template <class WebSocket>
Json::Value ConnectionImpl <WebSocket>::invokeCommand (
    Json::Value const& jvRequest, std::shared_ptr<JobQueue::Coro> coro)
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
    auto role = requestRole (required, m_port, jvRequest, m_remoteAddress,
        m_user);

    if (Role::FORBID == role)
    {
        jvResult[jss::result]  = rpcError (rpcFORBIDDEN);
    }
    else
    {
        RPC::Context context {app_.journal ("RPCHandler"), jvRequest,
            app_, loadType, m_netOPs, app_.getLedgerMaster(), getConsumer(),
                role, coro, this->shared_from_this(),
                    {m_user, m_forwardedFor}};
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

        // For testing resource limits on this connection.
        if (jvRequest[jss::command].asString() == "ping")
        {
            if (getConsumer().isUnlimited())
                jvResult[jss::unlimited] = true;
        }
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
    if (! m_forwardedFor.empty() || ! m_user.empty())
    {
        JLOG(j_.debug()) << "disconnect secure_gateway X-Forwarded-For: " <<
            m_forwardedFor << ", X-User: " << m_user;
    }

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
    JLOG (j_.debug()) <<
        "WebSocket: sending " << to_string (jvObj);
    connection_ptr ptr = m_connection.lock ();
    if (ptr)
        m_handler.send (ptr, jvObj, broadcast);
}

template <class WebSocket>
void ConnectionImpl <WebSocket>::disconnect ()
{
    JLOG (j_.debug()) <<
        "WebSocket: disconnecting";
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
