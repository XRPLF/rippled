//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_WSCONNECTION_H
#define RIPPLE_WSCONNECTION_H

//------------------------------------------------------------------------------

/** A Ripple WebSocket connection handler.
    This handles everything that is independent of the endpint_type.
*/
class WSConnection
    : public boost::enable_shared_from_this <WSConnection>
    , public InfoSub
    , public CountedObject <WSConnection>
    , public Uncopyable
{
public:
    static char const* getCountedObjectName () { return "WSConnection"; }

protected:
    typedef websocketpp::message::data::ptr message_ptr;

    WSConnection (InfoSub::Source& source, bool isPublic,
        std::string const& remoteIP, boost::asio::io_service& io_service);

    virtual ~WSConnection ();

    virtual void preDestroy () = 0;
    virtual void disconnect () = 0;

public:
    void onPong (const std::string&);
    void rcvMessage (message_ptr msg, bool& msgRejected, bool& runQueue);
    message_ptr getMessage ();
    void returnMessage (message_ptr ptr);
    Json::Value invokeCommand (Json::Value& jvRequest);

protected:
    bool const m_isPublic;
    std::string const m_remoteIP;
    LockType m_receiveQueueMutex;
    std::deque <message_ptr> m_receiveQueue;
    NetworkOPs& m_netOPs;
    LoadSource m_loadSource;
    boost::asio::deadline_timer m_pingTimer;
    bool m_sentPing;
    bool m_receiveQueueRunning;
    bool m_isDead;

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
    WSConnectionType (InfoSub::Source& source, server_type& serverHandler,
        connection_ptr const& cpConnection)
        : WSConnection (source,
            serverHandler.getPublic (),
            cpConnection->get_socket ().lowest_layer ().remote_endpoint ().address ().to_string (),
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
            ScopedLockType sl (m_receiveQueueMutex, __FILE__, __LINE__);
            m_isDead = true;
        }
    }

    static void destroy (boost::shared_ptr <WSConnectionType <endpoint_type> >)
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
        // FIXME: Must dispatch to strand
        connection_ptr ptr = m_connection.lock ();

        if (ptr)
            ptr->close (websocketpp::close::status::PROTOCOL_ERROR, "overload");
    }

    bool onPingTimer (std::string&)
    {
#ifdef DISCONNECT_ON_WEBSOCKET_PING_TIMEOUTS
        if (m_sentPing)
            return true; // causes connection to close
#endif

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
                boost::bind (&WSConnectionType <endpoint_type>::pingTimer,
                    m_connection, &m_serverHandler, boost::asio::placeholders::error)));
        }
    }

private:
    server_type& m_serverHandler;
    weak_connection_ptr m_connection;
};

#endif
