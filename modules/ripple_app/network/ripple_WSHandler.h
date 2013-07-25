//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_WSHANDLER_H_INCLUDED
#define RIPPLE_WSHANDLER_H_INCLUDED

extern bool serverOkay (std::string& reason);

template <typename endpoint_type>
class WSConnection;

// CAUTION: on_* functions are called by the websocket code while holding a lock

struct WSServerHandlerLog;

// A single instance of this object is made.
// This instance dispatches all events.  There is no per connection persistence.

template <typename endpoint_type>
class WSServerHandler
    : public endpoint_type::handler
    , LeakChecked <WSServerHandler <endpoint_type> >
{
public:
    typedef typename endpoint_type::handler::connection_ptr     connection_ptr;
    typedef typename endpoint_type::handler::message_ptr        message_ptr;
    typedef boost::shared_ptr< WSConnection<endpoint_type> >    wsc_ptr;

    // Private reasons to close.
    enum
    {
        crTooSlow   = 4000,     // Client is too slow.
    };

private:
    boost::shared_ptr<boost::asio::ssl::context>                            mCtx;

protected:
    boost::mutex                                                            mMapLock;
    // For each connection maintain an associated object to track subscriptions.
    boost::unordered_map<connection_ptr, boost::shared_ptr< WSConnection<endpoint_type> > > mMap;
    bool                                                                    mPublic;

public:
    WSServerHandler (boost::shared_ptr<boost::asio::ssl::context> spCtx, bool bPublic) : mCtx (spCtx), mPublic (bPublic)
    {
        if (getConfig ().WEBSOCKET_SECURE != 0)
        {
            basio::SslContext::initializeFromFile (
                *mCtx,
                getConfig ().WEBSOCKET_SSL_KEY,
                getConfig ().WEBSOCKET_SSL_CERT,
                getConfig ().WEBSOCKET_SSL_CHAIN);
        }
    }

    bool        getPublic ()
    {
        return mPublic;
    };

    boost::asio::ssl::context& getASIOContext ()
    {
        return *mCtx;
    }

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
        cpClient->get_strand ().post (BIND_TYPE (
                                          &WSServerHandler<endpoint_type>::ssend, cpClient, mpMessage));
    }

    void send (connection_ptr cpClient, const std::string& strMessage, bool broadcast)
    {
        cpClient->get_strand ().post (BIND_TYPE (
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
            boost::mutex::scoped_lock   sl (mMapLock);
            typename boost::unordered_map<connection_ptr, wsc_ptr>::iterator it = mMap.find (cpClient);

            if (it == mMap.end ())
                return;

            ptr = it->second;
        }
        std::string data ("ping");

        if (ptr->onPingTimer (data))
        {
            WriteLog (lsWARNING, WSServerHandlerLog) << "Connection pings out";
            cpClient->close (websocketpp::close::status::PROTOCOL_ERROR, "ping timeout");
        }
        else
            cpClient->ping (data);
    }

    void on_send_empty (connection_ptr cpClient)
    {
        wsc_ptr ptr;
        {
            boost::mutex::scoped_lock   sl (mMapLock);
            typename boost::unordered_map<connection_ptr, wsc_ptr>::iterator it = mMap.find (cpClient);

            if (it == mMap.end ())
                return;

            ptr = it->second;
        }

        ptr->onSendEmpty ();
    }

    void on_open (connection_ptr cpClient)
    {
        boost::mutex::scoped_lock   sl (mMapLock);

        try
        {
            mMap[cpClient]  = boost::make_shared< WSConnection<endpoint_type> > (this, cpClient);
        }
        catch (...)
        {
        }
    }

    void on_pong (connection_ptr cpClient, std::string data)
    {
        wsc_ptr ptr;
        {
            boost::mutex::scoped_lock   sl (mMapLock);
            typename boost::unordered_map<connection_ptr, wsc_ptr>::iterator it = mMap.find (cpClient);

            if (it == mMap.end ())
                return;

            ptr = it->second;
        }
        ptr->onPong (data);
    }

    void on_close (connection_ptr cpClient)
    {
        // we cannot destroy the connection while holding the map lock or we deadlock with pubLedger
        wsc_ptr ptr;
        {
            boost::mutex::scoped_lock   sl (mMapLock);
            typename boost::unordered_map<connection_ptr, wsc_ptr>::iterator it = mMap.find (cpClient);

            if (it == mMap.end ())
                return;

            ptr = it->second;       // prevent the WSConnection from being destroyed until we release the lock
            mMap.erase (it);
        }
        ptr->preDestroy (); // Must be done before we return

        // Must be done without holding the websocket send lock
        getApp().getJobQueue ().addJob (jtCLIENT, "WSClient::destroy",
                                       BIND_TYPE (&WSConnection<endpoint_type>::destroy, ptr));
    }

    void on_message (connection_ptr cpClient, message_ptr mpMessage)
    {
        wsc_ptr ptr;
        {
            boost::mutex::scoped_lock   sl (mMapLock);
            typename boost::unordered_map<connection_ptr, wsc_ptr>::iterator it = mMap.find (cpClient);

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
                WriteLog (lsDEBUG, WSServerHandlerLog) << "Ws:: Rejected("
                                                       << cpClient->get_socket ().lowest_layer ().remote_endpoint ().address ().to_string ()
                                                       << ") '" << mpMessage->get_payload () << "'";
            }
            catch (...)
            {
            }
        }

        if (bRunQ)
            getApp().getJobQueue ().addJob (jtCLIENT, "WSClient::command",
                                           BIND_TYPE (&WSServerHandler<endpoint_type>::do_messages, this, P_1, cpClient));
    }

    void do_messages (Job& job, connection_ptr cpClient)
    {
        wsc_ptr ptr;
        {
            boost::mutex::scoped_lock   sl (mMapLock);
            typename boost::unordered_map<connection_ptr, wsc_ptr>::iterator it = mMap.find (cpClient);

            if (it == mMap.end ())
                return;

            ptr = it->second;
        }

        for (int i = 0; i < 10; ++i)
        {
            message_ptr msg = ptr->getMessage ();

            if (!msg)
                return;

            do_message (job, cpClient, ptr, msg);
        }

        getApp().getJobQueue ().addJob (jtCLIENT, "WSClient::more",
                                       BIND_TYPE (&WSServerHandler<endpoint_type>::do_messages, this, P_1, cpClient));
    }

    void do_message (Job& job, const connection_ptr& cpClient, const wsc_ptr& conn, const message_ptr& mpMessage)
    {
        Json::Value     jvRequest;
        Json::Reader    jrReader;

        try
        {
            WriteLog (lsDEBUG, WSServerHandlerLog) << "Ws:: Receiving("
                                                   << cpClient->get_socket ().lowest_layer ().remote_endpoint ().address ().to_string ()
                                                   << ") '" << mpMessage->get_payload () << "'";
        }
        catch (...)
        {
        }

        if (mpMessage->get_opcode () != websocketpp::frame::opcode::TEXT)
        {
            Json::Value jvResult (Json::objectValue);

            jvResult["type"]    = "error";
            jvResult["error"]   = "wsTextRequired"; // We only accept text messages.

            send (cpClient, jvResult, false);
        }
        else if (!jrReader.parse (mpMessage->get_payload (), jvRequest) || jvRequest.isNull () || !jvRequest.isObject ())
        {
            Json::Value jvResult (Json::objectValue);

            jvResult["type"]    = "error";
            jvResult["error"]   = "jsonInvalid";    // Received invalid json.
            jvResult["value"]   = mpMessage->get_payload ();

            send (cpClient, jvResult, false);
        }
        else
        {
            if (jvRequest.isMember ("command"))
                job.rename (std::string ("WSClient::") + jvRequest["command"].asString ());

            send (cpClient, conn->invokeCommand (jvRequest), false);
        }
    }

    boost::shared_ptr<boost::asio::ssl::context> on_tls_init ()
    {
        return mCtx;
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

#endif

// vim:ts=4
