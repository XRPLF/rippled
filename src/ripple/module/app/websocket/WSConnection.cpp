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

#include <ripple/common/jsonrpc_fields.h>

namespace ripple {

SETUP_LOGN (WSConnection, "WSConnection")

//------------------------------------------------------------------------------

WSConnection::WSConnection (Resource::Manager& resourceManager,
    Resource::Consumer usage, InfoSub::Source& source, bool isPublic,
        beast::IP::Endpoint const& remoteAddress, boost::asio::io_service& io_service)
    : InfoSub (source, usage)
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

WSConnection::~WSConnection ()
{
}

void WSConnection::onPong (const std::string&)
{
    m_sentPing = false;
}

void WSConnection::rcvMessage (message_ptr msg, bool& msgRejected, bool& runQueue)
{
    ScopedLockType sl (m_receiveQueueMutex);

    if (m_isDead)
    {
        msgRejected = false;
        runQueue = false;
        return;
    }

    if ((m_receiveQueue.size () >= 1000) || (msg->get_payload().size() > 1000000))
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

void WSConnection::returnMessage (message_ptr ptr)
{
    ScopedLockType sl (m_receiveQueueMutex);

    if (!m_isDead)
    {
        m_receiveQueue.push_front (ptr);
        m_receiveQueueRunning = false;
    }
}

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
    RPCHandler  mRPCHandler (m_netOPs, std::dynamic_pointer_cast<InfoSub> (this->shared_from_this ()));
    Json::Value jvResult (Json::objectValue);

    Config::Role const role = m_isPublic
            ? Config::GUEST     // Don't check on the public interface.
            : getConfig ().getAdminRole (
                jvRequest, m_remoteAddress);

    if (Config::FORBID == role)
    {
        jvResult[jss::result]  = rpcError (rpcFORBIDDEN);
    }
    else
    {
        jvResult[jss::result] = mRPCHandler.doCommand (jvRequest, role, loadType);
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
        jvResult[jss::status]  = jss::success;
    }

    if (jvRequest.isMember (jss::id))
    {
        jvResult[jss::id]      = jvRequest[jss::id];
    }

    jvResult[jss::type]        = jss::response;

    return jvResult;
}

} // ripple
