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

#ifndef RIPPLE_APP_WEBSOCKET_WSSERVERHANDLER_H_INCLUDED
#define RIPPLE_APP_WEBSOCKET_WSSERVERHANDLER_H_INCLUDED

#include <ripple/app/main/Application.h>
#include <ripple/app/main/CollectorManager.h>
#include <ripple/core/JobQueue.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/server/Port.h>
#include <ripple/json/json_reader.h>
#include <ripple/websocket/Connection.h>
#include <ripple/websocket/WebSocket.h>
#include <boost/unordered_map.hpp>
#include <memory>

namespace ripple {

namespace websocket {

// CAUTION: on_* functions are called by the websocket code while holding a lock

// A single instance of this object is made.
// This instance dispatches all events.  There is no per connection persistence.

/** Make a beast endpoint from a boost::asio endpoint. */
inline
beast::IP::Endpoint makeBeastEndpoint (boost::asio::ip::tcp::endpoint const& e)
{
    return beast::IP::from_asio (e);
}

/** Make a beast endpoint from itself. */
inline
beast::IP::Endpoint makeBeastEndpoint (beast::IP::Endpoint const& e)
{
    return e;
}


template <class WebSocket>
class HandlerImpl
    : public WebSocket::Handler
{
public:
    using connection_ptr = typename WebSocket::ConnectionPtr;
    using message_ptr = typename WebSocket::MessagePtr;
    using wsc_ptr = std::shared_ptr <ConnectionImpl <WebSocket> >;

    // Private reasons to close.
    enum
    {
        crTooSlow   = 4000,     // Client is too slow.
    };

private:
    Application& app_;
    beast::insight::Counter rpc_requests_;
    beast::insight::Event rpc_io_;
    beast::insight::Event rpc_size_;
    beast::insight::Event rpc_time_;
    ServerDescription desc_;

protected:
    // VFALCO TODO Make this private.
    std::mutex mLock;

    // For each connection maintain an associated object to track subscriptions.
    using MapType = boost::unordered_map<connection_ptr, wsc_ptr>;
    MapType mMap;

public:
    HandlerImpl (ServerDescription const& desc)
        : app_ (desc.app)
        , desc_ (desc)
    {
        auto const& group (desc_.collectorManager.group ("rpc"));
        rpc_requests_ = group->make_counter ("requests");
        rpc_io_ = group->make_event ("io");
        rpc_size_ = group->make_event ("size");
        rpc_time_ = group->make_event ("time");
    }

    HandlerImpl(HandlerImpl const&) = delete;
    HandlerImpl& operator= (HandlerImpl const&) = delete;

    HTTP::Port const&
    port() const
    {
        return desc_.port;
    }

    void send (connection_ptr const& cpClient, message_ptr const& mpMessage)
    {
        try
        {
            cpClient->send (
                mpMessage->get_payload (), mpMessage->get_opcode ());
        }
        catch (...)
        {
            WebSocket::closeTooSlowClient (*cpClient, crTooSlow);
        }
    }

    void send (connection_ptr const& cpClient, std::string const& strMessage,
               bool broadcast)
    {
        try
        {
            WriteLog (broadcast ? lsTRACE : lsDEBUG, HandlerLog)
                    << "Ws:: Sending '" << strMessage << "'";

            cpClient->send (strMessage);
        }
        catch (...)
        {
            WebSocket::closeTooSlowClient (*cpClient, crTooSlow);
        }
    }

    void send (connection_ptr const& cpClient, Json::Value const& jvObj,
               bool broadcast)
    {
        send (cpClient, to_string (jvObj), broadcast);
    }

    void pingTimer (connection_ptr const& cpClient)
    {
        wsc_ptr ptr;
        {
            ScopedLockType sl (mLock);
            auto it = mMap.find (cpClient);

            if (it == mMap.end ())
                return;

            ptr = it->second;
        }
        std::string data ("ping");

        if (ptr->onPingTimer (data))
        {
            cpClient->terminate ({});
            try
            {
                WriteLog (lsDEBUG, HandlerLog) <<
                    "Ws:: ping_out(" <<
                    // TODO(tom): re-enable this logging.
                    // cpClient->get_socket ().remote_endpoint ().to_string ()
                     ")";
            }
            catch (...)
            {
            }
        }
        else
            cpClient->ping (data);
    }

    void on_send_empty (connection_ptr cpClient) override
    {
        wsc_ptr ptr;
        {
            ScopedLockType sl (mLock);
            auto it = mMap.find (cpClient);

            if (it == mMap.end ())
                return;

            ptr = it->second;
        }

        ptr->onSendEmpty ();
    }

    void on_open (connection_ptr cpClient) override
    {
        ScopedLockType   sl (mLock);

        try
        {
            auto remoteEndpoint = cpClient->get_socket ().remote_endpoint ();
            auto connection = std::make_shared <ConnectionImpl <WebSocket> > (
                desc_.app,
                desc_.resourceManager,
                desc_.source,
                *this,
                cpClient,
                makeBeastEndpoint (remoteEndpoint),
                WebSocket::getStrand (*cpClient).get_io_service ());
            connection->setPingTimer ();
            auto result = mMap.emplace (cpClient, std::move (connection));

            assert (result.second);
            (void) result.second;
            WriteLog (lsDEBUG, HandlerLog) <<
                "Ws:: on_open(" << remoteEndpoint << ")";
        }
        catch (...)
        {
        }
    }

    void on_pong (connection_ptr cpClient, std::string data) override
    {
        wsc_ptr ptr;
        {
            ScopedLockType   sl (mLock);
            auto it = mMap.find (cpClient);

            if (it == mMap.end ())
                return;

            ptr = it->second;
        }
        try
        {
            WriteLog (lsDEBUG, HandlerLog) <<
           "Ws:: on_pong(" << cpClient->get_socket ().remote_endpoint() << ")";
        }
        catch (...)
        {
        }
        ptr->onPong (data);
    }

    void on_close (connection_ptr cpClient) override
    {
        doClose (cpClient, "on_close");
    }

    void on_fail (connection_ptr cpClient) override
    {
        doClose (cpClient, "on_fail");
    }

    void doClose (connection_ptr const& cpClient, char const* reason)
    {
        // we cannot destroy the connection while holding the map lock or we
        // deadlock with pubLedger
        wsc_ptr ptr;
        {
            ScopedLockType   sl (mLock);
            auto it = mMap.find (cpClient);

            if (it == mMap.end ())
            {
                try
                {
                    WriteLog (lsDEBUG, HandlerLog) <<
                        "Ws:: " << reason << "(" <<
                           cpClient->get_socket ().remote_endpoint() <<
                           ") not found";
                }
                catch (...)
                {
                }
                return;
            }

            ptr = it->second;
            // prevent the ConnectionImpl from being destroyed until we release
            // the lock
            mMap.erase (it);
        }
        ptr->preDestroy (); // Must be done before we return
        try
        {
            WriteLog (lsDEBUG, HandlerLog) <<
                "Ws:: " << reason << "(" <<
                   cpClient->get_socket ().remote_endpoint () << ") found";
        }
        catch (...)
        {
        }

        // Must be done without holding the websocket send lock
        app_.getJobQueue ().addJob (
            jtCLIENT,
            "WSClient::destroy",
            [ptr] (Job&) { ConnectionImpl <WebSocket>::destroy(ptr); });
    }

    void message_job(std::string const& name,
                     connection_ptr const& cpClient)
    {
        auto msgs = [this, cpClient] (Job& j) { do_messages(j, cpClient); };
        app_.getJobQueue ().addJob (jtCLIENT, "WSClient::" + name, msgs);
    }

    void on_message (connection_ptr cpClient, message_ptr mpMessage) override
    {
        wsc_ptr ptr;
        {
            ScopedLockType   sl (mLock);
            auto it = mMap.find (cpClient);

            if (it == mMap.end ())
                return;

            ptr = it->second;
        }

        bool bRejected, bRunQ;
        ptr->rcvMessage (mpMessage, bRejected, bRunQ);

        if (bRejected)
        {
            try
            {
                WriteLog (lsDEBUG, HandlerLog) <<
                    "Ws:: Rejected(" <<
                    cpClient->get_socket().remote_endpoint() <<
                    ") '" << mpMessage->get_payload () << "'";
            }
            catch (...)
            {
            }
        }

        if (bRunQ)
            message_job("command", cpClient);
    }

    void do_messages (Job& job, connection_ptr const& cpClient)
    {
        wsc_ptr ptr;
        {
            ScopedLockType   sl (mLock);
            auto it = mMap.find (cpClient);

            if (it == mMap.end ())
                return;

            ptr = it->second;
        }

        // This loop prevents a single thread from handling more
        // than 3 operations for the same client, otherwise a client
        // can monopolize resources.
        //
        for (int i = 0; i < 3; ++i)
        {
            message_ptr msg = ptr->getMessage ();

            if (!msg)
                return;

            if (!do_message (job, cpClient, ptr, msg))
            {
                ptr->returnMessage(msg);
                return;
            }
        }

        if (ptr->checkMessage ())
            message_job("more", cpClient);
    }

    bool do_message (Job& job, const connection_ptr& cpClient,
                     const wsc_ptr& conn, const message_ptr& mpMessage)
    {
        Json::Value jvRequest;
        Json::Reader jrReader;

        try
        {
            WriteLog (lsDEBUG, HandlerLog)
                    << "Ws:: Receiving("
                    << cpClient->get_socket ().remote_endpoint ()
                    << ") '" << mpMessage->get_payload () << "'";
        }
        catch (...)
        {
        }

        if (!WebSocket::isTextMessage (*mpMessage))
        {
            Json::Value jvResult (Json::objectValue);

            jvResult[jss::type]    = jss::error;
            jvResult[jss::error]   = "wsTextRequired";
            // We only accept text messages.

            send (cpClient, jvResult, false);
        }
        else if (!jrReader.parse (mpMessage->get_payload (), jvRequest) ||
                 ! jvRequest || !jvRequest.isObject ())
        {
            Json::Value jvResult (Json::objectValue);

            jvResult[jss::type]    = jss::error;
            jvResult[jss::error]   = "jsonInvalid";    // Received invalid json.
            jvResult[jss::value]   = mpMessage->get_payload ();

            send (cpClient, jvResult, false);
        }
        else
        {
            if (jvRequest.isMember (jss::command))
            {
                Json::Value& jCmd = jvRequest[jss::command];
                if (jCmd.isString())
                    job.rename ("WSClient::" + jCmd.asString());
            }

            auto const start = std::chrono::high_resolution_clock::now ();

            struct HandlerCoroutineData
            {
                Json::Value jvRequest;
                std::string buffer;
                wsc_ptr conn;
            };

            auto data = std::make_shared<HandlerCoroutineData>();
            data->jvRequest = std::move(jvRequest);
            data->conn = conn;

            auto coroutine = [data] (RPC::Suspend const& suspend) {
                data->buffer = to_string(
                    data->conn->invokeCommand(data->jvRequest, suspend));
            };
            static auto const disableWebsocketsCoroutines = true;
            auto useCoroutines = disableWebsocketsCoroutines ?
                    RPC::UseCoroutines::no : RPC::useCoroutines(desc_.config);
            runOnCoroutine(useCoroutines, coroutine);

            rpc_time_.notify (static_cast <beast::insight::Event::value_type> (
                std::chrono::duration_cast <std::chrono::milliseconds> (
                    std::chrono::high_resolution_clock::now () - start)));
            ++rpc_requests_;
            rpc_size_.notify (static_cast <beast::insight::Event::value_type>
                (data->buffer.size()));
            send (cpClient, data->buffer, false);
        }

        return true;
    }

    boost::asio::ssl::context&
    get_ssl_context ()
    {
        return *port().context;
    }

    bool plain_only()
    {
        return port().protocol.count("wss") == 0;
    }

    bool secure_only()
    {
        return port().protocol.count("ws") == 0;
    }

    // Respond to http requests.
    bool http (connection_ptr cpClient) override
    {
        std::string reason;

        if (! app_.serverOkay (reason))
        {
            cpClient->set_body (
                "<HTML><BODY>Server cannot accept clients: " +
                reason + "</BODY></HTML>");
            return false;
        }

        cpClient->set_body (
            "<!DOCTYPE html><html><head><title>" + systemName () +
            " Test page for rippled</title></head>" + "<body><h1>" +
            systemName () + " Test</h1><p>This page shows rippled http(s) "
            "connectivity is working./p></body></html>");
        return true;
    }

    void recordMetrics (RPC::Context const& context) const
    {
        rpc_io_.notify (static_cast <beast::insight::Event::value_type> (
            context.metrics.fetches));
    }
};

} // websocket
} // ripple

#endif
