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

#include <ripple/common/RippleSSLContext.h>
#include <ripple/module/app/main/RPCHTTPServer.h>
#include <ripple/module/rpc/RPCHandler.h>
#include <ripple/module/rpc/RPCServerHandler.h>

namespace ripple {

class RPCHTTPServerImp
    : public RPCHTTPServer
    , public beast::LeakChecked <RPCHTTPServerImp>
    , public HTTP::Handler
{
public:
    Resource::Manager& m_resourceManager;
    beast::Journal m_journal;
    JobQueue& m_jobQueue;
    NetworkOPs& m_networkOPs;
    RPCServerHandler m_deprecatedHandler;
    HTTP::Server m_server;
    std::unique_ptr <RippleSSLContext> m_context;

    RPCHTTPServerImp (Stoppable& parent,
                      beast::Journal journal,
                      JobQueue& jobQueue,
                      NetworkOPs& networkOPs,
                      Resource::Manager& resourceManager)
        : RPCHTTPServer (parent)
        , m_resourceManager (resourceManager)
        , m_journal (journal)
        , m_jobQueue (jobQueue)
        , m_networkOPs (networkOPs)
        , m_deprecatedHandler (networkOPs, resourceManager)
        , m_server (*this, journal)
    {
        if (getConfig ().RPC_SECURE == 0)
        {
            m_context.reset (RippleSSLContext::createBare ());
        }
        else
        {
            m_context.reset (RippleSSLContext::createAuthenticated (
                getConfig ().RPC_SSL_KEY,
                    getConfig ().RPC_SSL_CERT,
                        getConfig ().RPC_SSL_CHAIN));
        }
    }

    ~RPCHTTPServerImp ()
    {
        m_server.stop();
    }

    void setup (beast::Journal journal)
    {
        if (! getConfig ().getRpcIP().empty () &&
              getConfig ().getRpcPort() != 0)
        {
            beast::IP::Endpoint ep (beast::IP::Endpoint::from_string (getConfig().getRpcIP()));
            if (! is_unspecified (ep))
            {
                HTTP::Port port;
                port.addr = ep.at_port(0);
                if (getConfig ().getRpcPort() != 0)
                    port.port = getConfig ().getRpcPort();
                else
                    port.port = ep.port();
                port.context = m_context.get ();

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

    void onStop ()
    {
        m_server.stopAsync();
    }

    void onChildrenStopped ()
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
            ! beast::IP::is_loopback (session.remoteAddress()))
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
        m_jobQueue.addJob (jtRPC, "RPC", std::bind (
            &RPCHTTPServerImp::processSession, this, std::placeholders::_1,
                std::ref (session)));
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
            session.content(), session.remoteAddress().at_port(0)));

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
    std::string processRequest (std::string const& request, beast::IP::Endpoint const& remoteIPAddress)
    {
        Json::Value jvRequest;
        {
            Json::Reader reader;

            if ((request.size () > 1000000) ||
                ! reader.parse (request, jvRequest) ||
                jvRequest.isNull () ||
                ! jvRequest.isObject ())
            {
                return createResponse (400, "Unable to parse request");
            }
        }

        Config::Role const role (getConfig ().getAdminRole (jvRequest, remoteIPAddress));

        Resource::Consumer usage;

        if (role == Config::ADMIN)
            usage = m_resourceManager.newAdminEndpoint (remoteIPAddress.to_string());
        else
            usage = m_resourceManager.newInboundEndpoint(remoteIPAddress);

        if (usage.disconnect ())
            return createResponse (503, "Server is overloaded");

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

        RPCHandler rpcHandler (m_networkOPs);

        Resource::Charge loadType = Resource::feeReferenceRPC;

        Json::Value const result (rpcHandler.doRpcCommand (
            strMethod, params, role, loadType));

        usage.charge (loadType);

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
                                   beast::Journal journal,
                                   JobQueue& jobQueue,
                                   NetworkOPs& networkOPs,
                                   Resource::Manager& resourceManager)
{
    return new RPCHTTPServerImp (parent, journal, jobQueue, networkOPs, resourceManager);
}

}
