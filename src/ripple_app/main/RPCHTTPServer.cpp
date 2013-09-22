//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

class RPCHTTPServerImp
    : public RPCHTTPServer
    , public LeakChecked <RPCHTTPServerImp>
    , public HTTPServer::Handler
{
public:
    NetworkOPs& m_networkOPs;
    RPCServerHandler m_deprecatedHandler;
    HTTPServer m_server;
    ScopedPointer <RippleSSLContext> m_context;

    RPCHTTPServerImp (Stoppable& parent,
                      Journal journal,
                      NetworkOPs& networkOPs)
        : RPCHTTPServer (parent)
        , m_networkOPs (networkOPs)
        , m_deprecatedHandler (networkOPs)
        , m_server (*this, journal)
    {
        if (getConfig ().RPC_SECURE == 0)
        {
            m_context = RippleSSLContext::createBare ();
        }
        else
        {
            m_context = RippleSSLContext::createAuthenticated (
                getConfig ().RPC_SSL_KEY,
                    getConfig ().RPC_SSL_CERT,
                        getConfig ().RPC_SSL_CHAIN);
        }
    }

    ~RPCHTTPServerImp ()
    {
        m_server.stop();
    }

    void setup (Journal journal)
    {
        if (! getConfig ().getRpcIP().empty () &&
              getConfig ().getRpcPort() != 0)
        {
            IPEndpoint ep (IPEndpoint::from_string (getConfig().getRpcIP()));
            if (! ep.empty())
            {
                HTTPServer::Port port;
                port.addr = ep.withPort(0);
                if (getConfig ().getRpcPort() != 0)
                    port.port = getConfig ().getRpcPort();
                else
                    port.port = ep.port();
                port.context = m_context;

                HTTPServer::Ports ports;
                ports.push_back (port);
                m_server.setPorts (ports);
            }
        }
        else
        {
            journal.info << "RPC interface: disabled";
        }
    }

    //--------------------------------------------------------------------------
    //
    // Stoppable
    //

    void onStop()
    {
        m_server.stopAsync();
    }

    void onChildrenStopped()
    {
    }

    //--------------------------------------------------------------------------
    //
    // HTTPServer::Handler
    //

    void onAccept (HTTPServer::Session& session)
    {
        // Reject non-loopback connections if RPC_ALLOW_REMOTE is not set
        if (! getConfig().RPC_ALLOW_REMOTE &&
            ! session.remoteAddress.isLoopback())
        {
            session.close();
        }
    }

    void onHeaders (HTTPServer::Session& session)
    {
    }

    void onRequest (HTTPServer::Session& session)
    {
        session.write (m_deprecatedHandler.processRequest (
            session.content, session.remoteAddress.to_string()));

        session.close();
    }

    void onClose (HTTPServer::Session& session)
    {
    }

    void onStopped (HTTPServer&)
    {
        stopped();
    }
};

//------------------------------------------------------------------------------

RPCHTTPServer::RPCHTTPServer (Stoppable& parent)
    : Stoppable ("RPCHTTPServer", parent)
{
}

//------------------------------------------------------------------------------

RPCHTTPServer* RPCHTTPServer::New (Stoppable& parent,
                                   Journal journal,
                                   NetworkOPs& networkOPs)
{
    return new RPCHTTPServerImp (parent, journal, networkOPs);
}

