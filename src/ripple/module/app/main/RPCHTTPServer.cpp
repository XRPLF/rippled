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
#include <ripple/common/jsonrpc_fields.h>
#include <ripple/http/Session.h>
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
            auto ep = beast::IP::Endpoint::from_string (getConfig().getRpcIP());

            // VFALCO TODO IP address should not have an "unspecified" state
            //if (! is_unspecified (ep))
            {
                HTTP::Port port;
                port.security = HTTP::Port::allow_ssl;
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

    // Returns negative for "small" jobs, 0 for "average" jobs and positive for
    // "big" jobs.
    int jobSize (HTTP::Session& session) const
    {
        if (!session.hasLegalJson())
            return 0;
        auto& json = session.getJson ();
        auto command = json[jss::command].asString ();

        // Use magic knowledge from ripple/module/rpc/impl/Handler.cpp
        // to identify a path finding request.

        if (command != "path_find")
            return 0;

        // TODO(tom): formalize this "cheap response" idea in the new RPC
        // handlers so I don't need magic knowledge.

        auto subcommand = json["subcommand"].asString ();

        if (subcommand == "close" || subcommand == "status")
            return -1;  // These are very cheap subcommands.

        // TODO(tom): make sure that close and status really are cheap.

        if (subcommand != "create")
            return 0;  // This shouldn't happen but we can handle this later.

        // It's a path request!

        // TODO(tom): figure out if this path request is light enough
        // to do right here.

        // TODO(tom): extract the code from somewhere around
        // ripple/module/app/paths/PathRequests.cpp:83-146 to allow us to
        // respond to a pathfinding request without going through the path
        // finding request queue.

        return 0;
    }

    void onRequest (HTTP::Session& session)
    {
        // Check user/password authorization
        auto const headers (session.request()->headers().build_map());
        if (! HTTPAuthorized (headers))
        {
            session.write (HTTPReply (403, "Forbidden"));
            session.close();
            return;
        }

        if (jobSize (session) < 0)
        {
            // Small jobs bypass the job queue.
            Job job;
            processSession (job, session);
        }
        else
        {
            // Jobs that are average or larger use the job queue.
            session.detach();

            m_jobQueue.addJob (jtCLIENT, "RPC-Client", std::bind (
                &RPCHTTPServerImp::processSession, this, std::placeholders::_1,
                std::ref (session)));
        }
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
        if (session.hasLegalJson())
        {
            session.write (
                processRequest (
                    session.getJson(), session.remoteAddress().at_port(0)));
        }
        else
        {
            session.write (createResponse (400, "Unable to parse request"));
        }
        session.close();
    }

    std::string createResponse (
        int statusCode,
        std::string const& description)
    {
        return HTTPReply (statusCode, description);
    }

    std::string processRequest (
        Json::Value const& jvRequest,
        beast::IP::Endpoint const& remoteIPAddress)
    {
        Config::Role const role (
            getConfig ().getAdminRole (jvRequest, remoteIPAddress));

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
            return createResponse (400, "Null method");
        if (! method.isString ())
            return createResponse (400, "method is not string");

        std::string strMethod = method.asString ();

        // Parse params
        Json::Value params = jvRequest ["params"];

        if (params.isNull ())
            params = Json::Value (Json::arrayValue);
        else if (!params.isArray ())
            return HTTPReply (400, "params unparseable");

        // VFALCO TODO Shouldn't we handle this earlier?
        //
        if (role == Config::FORBID)
            return HTTPReply (403, "Forbidden");

        // VFALCO TODO Needs implementing
        // FIXME Needs implementing
        // XXX This needs rate limiting to prevent brute forcing password.

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

RPCHTTPServer* RPCHTTPServer::New (
    Stoppable& parent,
    beast::Journal journal,
    JobQueue& jobQueue,
    NetworkOPs& networkOPs,
    Resource::Manager& resourceManager)
{
    return new RPCHTTPServerImp (
        parent, journal, jobQueue, networkOPs, resourceManager);
}

}
