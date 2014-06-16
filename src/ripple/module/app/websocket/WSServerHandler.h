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

#ifndef RIPPLE_WSSERVERHANDLER_H_INCLUDED
#define RIPPLE_WSSERVERHANDLER_H_INCLUDED

#include <ripple/common/jsonrpc_fields.h>
#include <ripple/module/app/websocket/WSConnection.h>

namespace ripple {

extern bool serverOkay (std::string& reason);

template <typename endpoint_type>
class WSConnectionType;

// CAUTION: on_* functions are called by the websocket code while holding a lock

struct WSServerHandlerLog;

// This tag helps with mutex tracking
struct WSServerHandlerBase : public beast::Uncopyable
{
};

// A single instance of this object is made.
// This instance dispatches all events.  There is no per connection persistence.

template <typename endpoint_type>
class WSServerHandler
    : public WSServerHandlerBase
    , public endpoint_type::handler
    , public beast::LeakChecked <WSServerHandler <endpoint_type> >
{
public:
    typedef typename endpoint_type::handler::connection_ptr     connection_ptr;
    typedef typename endpoint_type::handler::message_ptr        message_ptr;
    typedef std::shared_ptr< WSConnectionType <endpoint_type> >    wsc_ptr;

    // Private reasons to close.
    enum
    {
        crTooSlow   = 4000,     // Client is too slow.
    };

private:
    Resource::Manager& m_resourceManager;
    InfoSub::Source& m_source;

protected:
    // VFALCO TODO Make this private.
    typedef RippleMutex LockType;
    typedef std::lock_guard <LockType> ScopedLockType;
    LockType mLock;

private:
    boost::asio::ssl::context& m_ssl_context;

protected:
    // For each connection maintain an associated object to track subscriptions.
    typedef ripple::unordered_map <connection_ptr,
        std::shared_ptr <WSConnectionType <endpoint_type> > > MapType;
    MapType mMap;
    bool const mPublic;
    bool const mProxy;

public:
    WSServerHandler (Resource::Manager& resourceManager,
        InfoSub::Source& source, boost::asio::ssl::context& ssl_context, bool bPublic, bool bProxy)
        : m_resourceManager (resourceManager)
        , m_source (source)
        , m_ssl_context (ssl_context)
        , mPublic (bPublic)
        , mProxy (bProxy)
    {
    }

    bool getPublic ()
    {
        return mPublic;
    };

    static void ssend (connection_ptr cpClient, message_ptr mpMessage)
    {
        try
        {
            cpClient->send (mpMessage->get_payload (), mpMessage->get_opcode ());
        }
        catch (...)
        {
            cpClient->close (websocketpp::close::status::value (crTooSlow), std::string ("Client is too slow."));
        }
    }

    static void ssendb (connection_ptr cpClient, const std::string& strMessage, bool broadcast)
    {
        try
        {
            WriteLog (broadcast ? lsTRACE : lsDEBUG, WSServerHandlerLog) << "Ws:: Sending '" << strMessage << "'";

            cpClient->send (strMessage);
        }
        catch (...)
        {
            cpClient->close (websocketpp::close::status::value (crTooSlow), std::string ("Client is too slow."));
        }
    }

    void send (connection_ptr cpClient, message_ptr mpMessage)
    {
        cpClient->get_strand ().post (std::bind (
                                          &WSServerHandler<endpoint_type>::ssend, cpClient, mpMessage));
    }

    void send (connection_ptr cpClient, const std::string& strMessage, bool broadcast)
    {
        cpClient->get_strand ().post (std::bind (
                                          &WSServerHandler<endpoint_type>::ssendb, cpClient, strMessage, broadcast));
    }

    void send (connection_ptr cpClient, const Json::Value& jvObj, bool broadcast)
    {
        Json::FastWriter    jfwWriter;

        // WriteLog (lsDEBUG, WSServerHandlerLog) << "Ws:: Object '" << jfwWriter.write(jvObj) << "'";

        send (cpClient, jfwWriter.write (jvObj), broadcast);
    }

    void pingTimer (connection_ptr cpClient)
    {
        wsc_ptr ptr;
        {
            ScopedLockType sl (mLock);
            typename ripple::unordered_map<connection_ptr, wsc_ptr>::iterator it = mMap.find (cpClient);

            if (it == mMap.end ())
                return;

            ptr = it->second;
        }
        std::string data ("ping");

        if (ptr->onPingTimer (data))
        {
            cpClient->terminate (false);
            try
            {
                WriteLog (lsDEBUG, WSServerHandlerLog) <<
                    "Ws:: ping_out(" << cpClient->get_socket ().remote_endpoint ().to_string () << ")";
            }
            catch (...)
            {
            }
        }
        else
            cpClient->ping (data);
    }

    void on_send_empty (connection_ptr cpClient)
    {
        wsc_ptr ptr;
        {
            ScopedLockType sl (mLock);
            typename ripple::unordered_map<connection_ptr, wsc_ptr>::iterator it = mMap.find (cpClient);

            if (it == mMap.end ())
                return;

            ptr = it->second;
        }

        ptr->onSendEmpty ();
    }

    void on_open (connection_ptr cpClient)
    {
        ScopedLockType   sl (mLock);

        try
        {
            std::pair <typename MapType::iterator, bool> const result (
                mMap.emplace (cpClient,
                    std::make_shared < WSConnectionType <endpoint_type> > (std::ref(m_resourceManager),
                    std::ref (m_source), std::ref(*this), std::cref(cpClient))));
            assert (result.second);
            WriteLog (lsDEBUG, WSServerHandlerLog) <<
                "Ws:: on_open(" << cpClient->get_socket ().remote_endpoint ().to_string () << ")";
        }
        catch (...)
        {
        }
    }

    void on_pong (connection_ptr cpClient, std::string data)
    {
        wsc_ptr ptr;
        {
            ScopedLockType   sl (mLock);
            typename ripple::unordered_map<connection_ptr, wsc_ptr>::iterator it = mMap.find (cpClient);

            if (it == mMap.end ())
                return;

            ptr = it->second;
        }
        try
        {
            WriteLog (lsDEBUG, WSServerHandlerLog) <<
           "Ws:: on_pong(" << cpClient->get_socket ().remote_endpoint ().to_string () << ")";
        }
        catch (...)
        {
        }
        ptr->onPong (data);
    }

    void on_close (connection_ptr cpClient)
    {
        doClose (cpClient, "on_close");
    }

    void on_fail (connection_ptr cpClient)
    {
        doClose (cpClient, "on_fail");
    }

    void doClose (connection_ptr const& cpClient, char const* reason)
    {
        // we cannot destroy the connection while holding the map lock or we deadlock with pubLedger
        wsc_ptr ptr;
        {
            ScopedLockType   sl (mLock);
            typename ripple::unordered_map<connection_ptr, wsc_ptr>::iterator it = mMap.find (cpClient);

            if (it == mMap.end ())
            {
                try
                {
                    WriteLog (lsDEBUG, WSServerHandlerLog) <<
                        "Ws:: " << reason << "(" <<
                           cpClient->get_socket ().remote_endpoint ().to_string () << ") not found";
                }
                catch (...)
                {
                }
                return;
            }

            ptr = it->second;       // prevent the WSConnection from being destroyed until we release the lock
            mMap.erase (it);
        }
        ptr->preDestroy (); // Must be done before we return
        try
        {
            WriteLog (lsDEBUG, WSServerHandlerLog) <<
                "Ws:: " << reason << "(" <<
                   cpClient->get_socket ().remote_endpoint ().to_string () << ") found";
        }
        catch (...)
        {
        }

        // Must be done without holding the websocket send lock
        getApp().getJobQueue ().addJob (jtCLIENT, "WSClient::destroy",
                                       std::bind (&WSConnectionType <endpoint_type>::destroy, ptr));
    }

    void on_message (connection_ptr cpClient, message_ptr mpMessage)
    {
        wsc_ptr ptr;
        {
            ScopedLockType   sl (mLock);
            typename ripple::unordered_map<connection_ptr, wsc_ptr>::iterator it = mMap.find (cpClient);

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
                WriteLog (lsDEBUG, WSServerHandlerLog) <<
                    "Ws:: Rejected(" << to_string (cpClient->get_socket ().remote_endpoint ()) <<
                    ") '" << mpMessage->get_payload () << "'";
            }
            catch (...)
            {
            }
        }

        if (bRunQ)
            getApp().getJobQueue ().addJob (jtCLIENT, "WSClient::command",
                      std::bind (&WSServerHandler<endpoint_type>::do_messages,
                                 this, std::placeholders::_1, cpClient));
    }

    void do_messages (Job& job, connection_ptr cpClient)
    {
        wsc_ptr ptr;
        {
            ScopedLockType   sl (mLock);
            typename ripple::unordered_map<connection_ptr, wsc_ptr>::iterator it = mMap.find (cpClient);

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
            getApp().getJobQueue ().addJob (
                jtCLIENT, "WSClient::more",
                std::bind (&WSServerHandler<endpoint_type>::do_messages, this,
                           std::placeholders::_1, cpClient));
    }

    bool do_message (Job& job, const connection_ptr& cpClient, const wsc_ptr& conn, const message_ptr& mpMessage)
    {
        Json::Value     jvRequest;
        Json::Reader    jrReader;

        try
        {
            WriteLog (lsDEBUG, WSServerHandlerLog) <<
                "Ws:: Receiving(" << to_string (cpClient->get_socket ().remote_endpoint ()) <<
                ") '" << mpMessage->get_payload () << "'";
        }
        catch (...)
        {
        }

        if (mpMessage->get_opcode () != websocketpp::frame::opcode::TEXT)
        {
            Json::Value jvResult (Json::objectValue);

            jvResult[jss::type]    = jss::error;
            jvResult[jss::error]   = "wsTextRequired"; // We only accept text messages.

            send (cpClient, jvResult, false);
        }
        else if (!jrReader.parse (mpMessage->get_payload (), jvRequest) || jvRequest.isNull () || !jvRequest.isObject ())
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
                    job.rename (std::string ("WSClient::") + jCmd.asString());
            }

            send (cpClient, conn->invokeCommand (jvRequest), false);
        }

        return true;
    }

    boost::asio::ssl::context& get_ssl_context ()
    {
        return m_ssl_context;
    }
    bool get_proxy ()
    {
        return mProxy;
    }

    // Respond to http requests.
    bool http (connection_ptr cpClient)
    {
        std::string reason;

        if (!serverOkay (reason))
        {
            cpClient->set_body (std::string ("<HTML><BODY>Server cannot accept clients: ") + reason + "</BODY></HTML>");
            return false;
        }

        cpClient->set_body (
            "<!DOCTYPE html><html><head><title>" SYSTEM_NAME " Test</title></head>"
            "<body><h1>" SYSTEM_NAME " Test</h1><p>This page shows http(s) connectivity is working.</p></body></html>");
        return true;
    }
};

} // ripple

#endif
