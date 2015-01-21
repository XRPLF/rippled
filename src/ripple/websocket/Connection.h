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
#include <ripple/core/Config.h>
#include <ripple/json/to_string.h>
#include <ripple/net/InfoSub.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/resource/Fees.h>
#include <ripple/resource/Manager.h>
#include <ripple/rpc/RPCHandler.h>
#include <ripple/server/Port.h>
#include <ripple/json/to_string.h>
#include <ripple/rpc/RPCHandler.h>
#include <ripple/server/Role.h>
#include <boost/asio.hpp>
#include <beast/asio/placeholders.h>
#include <memory>

namespace ripple {

/** A Ripple WebSocket connection handler.
    This handles everything that is independent of the endpint_type.
*/
class WSConnection
    : public std::enable_shared_from_this <WSConnection>
    , public InfoSub
    , public CountedObject <WSConnection>
{
public:
    static char const* getCountedObjectName () { return "WSConnection"; }

protected:
    typedef websocketpp_02::message::data::ptr message_ptr;

    WSConnection (HTTP::Port const& port,
        Resource::Manager& resourceManager, Resource::Consumer usage,
            InfoSub::Source& source, bool isPublic,
                beast::IP::Endpoint const& remoteAddress,
                    boost::asio::io_service& io_service);

    WSConnection(WSConnection const&) = delete;
    WSConnection& operator= (WSConnection const&) = delete;

    virtual ~WSConnection ();

    virtual void preDestroy () = 0;
    virtual void disconnect () = 0;

    virtual void recordMetrics (RPC::Context const&) const = 0;

public:
    void onPong (std::string const&);
    void rcvMessage (message_ptr msg, bool& msgRejected, bool& runQueue);
    message_ptr getMessage ();
    bool checkMessage ();
    void returnMessage (message_ptr ptr);
    Json::Value invokeCommand (Json::Value& jvRequest);

protected:
    HTTP::Port const& port_;
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

private:
    server_type& m_serverHandler;
    weak_connection_ptr m_connection;

public:
    WSConnectionType (Resource::Manager& resourceManager,
                      InfoSub::Source& source,
                      server_type& serverHandler,
                      connection_ptr const& cpConnection)
        : WSConnection (
            serverHandler.port(),
            resourceManager,
            resourceManager.newInboundEndpoint (
                cpConnection->get_socket ().remote_endpoint ()),
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

    void recordMetrics (RPC::Context const& context) const override
    {
        m_serverHandler.recordMetrics (context);
    }

    // Implement overridden functions from base class:
    void send (Json::Value const& jvObj, bool broadcast)
    {
        connection_ptr ptr = m_connection.lock ();

        if (ptr)
            m_serverHandler.send (ptr, jvObj, broadcast);
    }

    void send (Json::Value const& jvObj, std::string const& sObj, bool broadcast)
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
            ptr->close (websocketpp_02::close::status::PROTOCOL_ERROR, "overload");
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
                    m_connection, &m_serverHandler,
                           beast::asio::placeholders::error)));
        }
    }
};

//------------------------------------------------------------------------------
// This next code will become templated in the next change so these methods are
// brought here to simplify diffs.

inline
WSConnection::WSConnection (HTTP::Port const& port,
    Resource::Manager& resourceManager, Resource::Consumer usage,
        InfoSub::Source& source, bool isPublic,
            beast::IP::Endpoint const& remoteAddress,
                boost::asio::io_service& io_service)
    : InfoSub (source, usage)
    , port_(port)
    , m_resourceManager (resourceManager)
    , m_isPublic (isPublic)
    , m_remoteAddress (remoteAddress)
    , m_netOPs (getApp ().getOPs ())
    , m_pingTimer (io_service)
    , m_sentPing (false)
    , m_receiveQueueRunning (false)
    , m_isDead (false)
    , m_io_service (io_service)
{
    WriteLog (lsDEBUG, WSConnection) <<
        "Websocket connection from " << remoteAddress;
}

inline
WSConnection::~WSConnection ()
{
}

inline
void WSConnection::onPong (std::string const&)
{
    m_sentPing = false;
}

inline
void WSConnection::rcvMessage (
    message_ptr msg, bool& msgRejected, bool& runQueue)
{
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

inline
bool WSConnection::checkMessage ()
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

inline
WSConnection::message_ptr WSConnection::getMessage ()
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

inline
void WSConnection::returnMessage (message_ptr ptr)
{
    ScopedLockType sl (m_receiveQueueMutex);

    if (!m_isDead)
    {
        m_receiveQueue.push_front (ptr);
        m_receiveQueueRunning = false;
    }
}

inline
Json::Value WSConnection::invokeCommand (Json::Value& jvRequest)
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
    Role const role = requestRole (required, port_, jvRequest, m_remoteAddress,
                                   getConfig().RPC_ADMIN_ALLOW);

    if (Role::FORBID == role)
    {
        jvResult[jss::result]  = rpcError (rpcFORBIDDEN);
    }
    else
    {
        RPC::Context context {
            jvRequest, loadType, m_netOPs, role,
            std::dynamic_pointer_cast<InfoSub> (this->shared_from_this ())};
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


} // ripple

#endif
