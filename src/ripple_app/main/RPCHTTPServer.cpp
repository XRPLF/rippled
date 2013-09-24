//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

class RPCHTTPServerImp
    : public RPCHTTPServer
    , public LeakChecked <RPCHTTPServerImp>
    , public HTTP::Handler
{
public:
    Journal m_journal;
    JobQueue& m_jobQueue;
    NetworkOPs& m_networkOPs;
    RPCServerHandler m_deprecatedHandler;
    HTTP::Server m_server;
    ScopedPointer <RippleSSLContext> m_context;

    RPCHTTPServerImp (Stoppable& parent,
                      Journal journal,
                      JobQueue& jobQueue,
                      NetworkOPs& networkOPs)
        : RPCHTTPServer (parent)
        , m_journal (journal)
        , m_jobQueue (jobQueue)
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
                HTTP::Port port;
                port.addr = ep.withPort(0);
                if (getConfig ().getRpcPort() != 0)
                    port.port = getConfig ().getRpcPort();
                else
                    port.port = ep.port();
                port.context = m_context;

                HTTP::Ports ports;
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
    // HTTP::Handler
    //

    void onAccept (HTTP::Session& session)
    {
        // Reject non-loopback connections if RPC_ALLOW_REMOTE is not set
        if (! getConfig().RPC_ALLOW_REMOTE &&
            ! session.remoteAddress().isLoopback())
        {
            session.close();
        }
    }

    void onHeaders (HTTP::Session& session)
    {
    }

    void onRequest (HTTP::Session& session)
    {
#if 0
        Job job;
        processSession (job, session);
#else
        session.detach();

        // The "boost::"'s are a workaround for broken versions of tr1::functional that
        // require the reference wrapper to be callable. HTTP::Session has abstract functions
        // and so references to it are not callable.
        m_jobQueue.addJob (jtRPC, "RPC", boost::bind (
            &RPCHTTPServerImp::processSession, this, boost::_1,
                boost::ref (session)));
#endif
    }

    void onClose (HTTP::Session& session, int errorCode)
    {
    }

    void onStopped (HTTP::Server&)
    {
        stopped();
    }

    //--------------------------------------------------------------------------

    void processSession (Job& job, HTTP::Session& session)
    {
        session.write (m_deprecatedHandler.processRequest (
            session.content(), session.remoteAddress().withPort(0).to_string()));

        session.close();
    }

    std::string createResponse (
        int statusCode,
        std::string const& description)
    {
        return HTTPReply (statusCode, description);
    }

    bool isAuthorized (
        std::map <std::string, std::string> const& headers)
    {
        return HTTPAuthorized (headers);
    }

    // Stolen directly from RPCServerHandler
    std::string processRequest (std::string const& request, std::string const& remoteAddress)
    {
        Json::Value jvRequest;
        {
            Json::Reader reader;

            if (! reader.parse (request, jvRequest) ||
                jvRequest.isNull () ||
                ! jvRequest.isObject ())
            {
                return createResponse (400, "Unable to parse request");
            }
        }

        Config::Role const role (getConfig ().getAdminRole (jvRequest, remoteAddress));

        // Parse id now so errors from here on will have the id
        //
        // VFALCO NOTE Except that "id" isn't included in the following errors...
        //
        Json::Value const id = jvRequest ["id"];

        Json::Value const method = jvRequest ["method"];

        if (method.isNull ())
        {
            return createResponse (400, "Null method");
        }
        else if (! method.isString ())
        {
            return createResponse (400, "method is not string");
        }

        std::string strMethod = method.asString ();

        // Parse params
        Json::Value params = jvRequest ["params"];

        if (params.isNull ())
        {
            params = Json::Value (Json::arrayValue);
        }
        else if (!params.isArray ())
        {
            return HTTPReply (400, "params unparseable");
        }

        // VFALCO TODO Shouldn't we handle this earlier?
        //
        if (role == Config::FORBID)
        {
            // VFALCO TODO Needs implementing
            // FIXME Needs implementing
            // XXX This needs rate limiting to prevent brute forcing password.
            return HTTPReply (403, "Forbidden");
        }

        // This code does all the work on the io_service thread and
        // has no rate-limiting based on source IP or anything.
        // This is a temporary safety
        if ((role != Config::ADMIN) && (getApp().getFeeTrack().isLoadedLocal()))
        {
            return HTTPReply (503, "Unable to service at this time");
        }

        std::string response;

        m_journal.debug << "Query: " << strMethod << params;

        RPCHandler rpcHandler (&m_networkOPs);

        LoadType loadType = LT_RPCReference;

        Json::Value const result (rpcHandler.doRpcCommand (
            strMethod, params, role, &loadType));
        // VFALCO NOTE We discard loadType since there is no endpoint to punish

        m_journal.debug << "Reply: " << result;

        response = JSONRPCReply (result, Json::Value (), id);

        return createResponse (200, response);
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
                                   JobQueue& jobQueue,
                                   NetworkOPs& networkOPs)
{
    return new RPCHTTPServerImp (parent, journal, jobQueue, networkOPs);
}

