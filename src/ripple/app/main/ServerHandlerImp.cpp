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

#include <ripple/app/main/ServerHandlerImp.h>

namespace ripple {

ServerHandler::ServerHandler (Stoppable& parent)
    : Stoppable ("ServerHandler", parent)
    , Source ("server")
{
}

//------------------------------------------------------------------------------

ServerHandlerImp::ServerHandlerImp (Stoppable& parent, JobQueue& jobQueue,
    NetworkOPs& networkOPs, Resource::Manager& resourceManager,
        RPC::Setup const& setup)
    : ServerHandler (parent)
    , m_resourceManager (resourceManager)
    , m_journal (deprecatedLogs().journal("Server"))
    , m_jobQueue (jobQueue)
    , m_networkOPs (networkOPs)
    , m_server (HTTP::make_Server(
        *this, deprecatedLogs().journal("Server")))
    , setup_ (setup)
{
    if (setup_.secure)
        m_context.reset (RippleSSLContext::createAuthenticated (
            setup_.ssl_key, setup_.ssl_cert, setup_.ssl_chain));
    else
        m_context.reset (RippleSSLContext::createBare());
}

ServerHandlerImp::~ServerHandlerImp()
{
    m_server = nullptr;
}

void
ServerHandlerImp::setup (beast::Journal journal)
{
    if (! setup_.ip.empty() && setup_.port != 0)
    {
        auto ep = beast::IP::Endpoint::from_string (setup_.ip);

        // VFALCO TODO IP address should not have an "unspecified" state
        //if (! is_unspecified (ep))
        {
            HTTP::Port port;

            if (setup_.secure == 0)
                port.security = HTTP::Port::Security::no_ssl;
            else if (setup_.secure == 1)
                port.security = HTTP::Port::Security::allow_ssl;
            else
                port.security = HTTP::Port::Security::require_ssl;
            port.addr = ep.at_port(0);
            if (setup_.port != 0)
                port.port = setup_.port;
            else
                port.port = ep.port();
            port.context = m_context.get ();

            std::vector<HTTP::Port> list;
            list.push_back (port);
            m_server->ports(list);
        }
    }
    else
    {
        journal.info << "RPC interface: disabled";
    }
}

//--------------------------------------------------------------------------

void
ServerHandlerImp::onStop()
{
    m_server->close();
}

//--------------------------------------------------------------------------

void
ServerHandlerImp::onAccept (HTTP::Session& session)
{
    // Reject non-loopback connections if RPC_ALLOW_REMOTE is not set
    if (! setup_.allow_remote &&
        ! beast::IP::is_loopback (session.remoteAddress()))
    {
        session.close (false);
    }
}

void
ServerHandlerImp::onRequest (HTTP::Session& session)
{
    // Check user/password authorization
    auto const headers = build_map (session.request().headers);
    if (! HTTPAuthorized (headers))
    {
        session.write (HTTPReply (403, "Forbidden"));
        session.close (true);
        return;
    }

    m_jobQueue.addJob (jtCLIENT, "RPC-Client", std::bind (
        &ServerHandlerImp::processSession, this, std::placeholders::_1,
            session.detach()));
}

void
ServerHandlerImp::onClose (HTTP::Session& session,
    boost::system::error_code const&)
{
}

void
ServerHandlerImp::onStopped (HTTP::Server&)
{
    stopped();
}

//--------------------------------------------------------------------------

// Dispatched on the job queue
void
ServerHandlerImp::processSession (Job& job,
    std::shared_ptr<HTTP::Session> const& session)
{
    session->write (processRequest (to_string(session->body()),
        session->remoteAddress().at_port(0)));

    if (session->request().keep_alive())
    {
        session->complete();
    }
    else
    {
        session->close (true);
    }
}

std::string
ServerHandlerImp::createResponse (
    int statusCode,
    std::string const& description)
{
    return HTTPReply (statusCode, description);
}

// VFALCO ARGH! returning a single std::string for the entire response?
std::string
ServerHandlerImp::processRequest (std::string const& request,
    beast::IP::Endpoint const& remoteIPAddress)
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

    auto const role = getConfig ().getAdminRole (jvRequest, remoteIPAddress);

    Resource::Consumer usage;

    if (role == Config::ADMIN)
        usage = m_resourceManager.newAdminEndpoint (remoteIPAddress.to_string());
    else
        usage = m_resourceManager.newInboundEndpoint(remoteIPAddress);

    if (usage.disconnect ())
        return createResponse (503, "Server is overloaded");

    // Parse id now so errors from here on will have the id
    //
    // VFALCO NOTE Except that "id" isn't included in the following errors.
    //
    Json::Value const id = jvRequest ["id"];

    Json::Value const method = jvRequest ["method"];

    if (method.isNull ())
        return createResponse (400, "Null method");

    if (! method.isString ())
        return createResponse (400, "method is not string");

    std::string strMethod = method.asString ();
    if (strMethod.empty())
        return createResponse (400, "method is empty");

    // Parse params
    Json::Value params = jvRequest ["params"];

    if (params.isNull ())
        params = Json::Value (Json::arrayValue);

    else if (!params.isArray ())
        return HTTPReply (400, "params unparseable");

    // VFALCO TODO Shouldn't we handle this earlier?
    //
    if (role == Config::FORBID)
    {
        // VFALCO TODO Needs implementing
        // FIXME Needs implementing
        // XXX This needs rate limiting to prevent brute forcing password.
        return HTTPReply (403, "Forbidden");
    }


    std::string response;
    RPCHandler rpcHandler (m_networkOPs);
    Resource::Charge loadType = Resource::feeReferenceRPC;

    m_journal.debug << "Query: " << strMethod << params;

    Json::Value const result (rpcHandler.doRpcCommand (
        strMethod, params, role, loadType));
    m_journal.debug << "Reply: " << result;

    usage.charge (loadType);

    response = JSONRPCReply (result, Json::Value (), id);

    return createResponse (200, response);
}

//------------------------------------------------------------------------------

void
ServerHandlerImp::onWrite (beast::PropertyStream::Map& map)
{
    m_server->onWrite (map);
}

//------------------------------------------------------------------------------

std::unique_ptr <ServerHandler>
make_RPCHTTPServer (beast::Stoppable& parent, JobQueue& jobQueue,
    NetworkOPs& networkOPs, Resource::Manager& resourceManager,
        RPC::Setup const& setup)
{
    return std::make_unique <ServerHandlerImp> (
        parent, jobQueue, networkOPs, resourceManager, setup);
}

}
