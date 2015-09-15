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

#include <BeastConfig.h>
#include <ripple/app/main/Application.h>
#include <ripple/json/json_reader.h>
#include <ripple/server/JsonWriter.h>
#include <ripple/server/make_ServerHandler.h>
#include <ripple/server/impl/JSONRPCUtil.h>
#include <ripple/server/impl/ServerHandlerImp.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/make_SSLContext.h>
#include <ripple/core/JobQueue.h>
#include <ripple/server/make_Server.h>
#include <ripple/overlay/Overlay.h>
#include <ripple/resource/ResourceManager.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/Coroutine.h>
#include <beast/crypto/base64.h>
#include <ripple/rpc/RPCHandler.h>
#include <beast/cxx14/algorithm.h> // <algorithm>
#include <beast/http/rfc2616.h>
#include <boost/algorithm/string.hpp>
#include <boost/type_traits.hpp>
#include <boost/optional.hpp>
#include <boost/regex.hpp>
#include <algorithm>
#include <stdexcept>

namespace ripple {

ServerHandler::ServerHandler (Stoppable& parent)
    : Stoppable ("ServerHandler", parent)
    , Source ("server")
{
}

//------------------------------------------------------------------------------

ServerHandlerImp::ServerHandlerImp (Application& app, Stoppable& parent,
    boost::asio::io_service& io_service, JobQueue& jobQueue,
        NetworkOPs& networkOPs, Resource::Manager& resourceManager,
            CollectorManager& cm)
    : ServerHandler (parent)
    , app_ (app)
    , m_resourceManager (resourceManager)
    , m_journal (deprecatedLogs().journal("Server"))
    , m_networkOPs (networkOPs)
    , m_server (HTTP::make_Server(
        *this, io_service, deprecatedLogs().journal("Server")))
    , m_jobQueue (jobQueue)
{
    auto const& group (cm.group ("rpc"));
    rpc_requests_ = group->make_counter ("requests");
    rpc_io_ = group->make_event ("io");
    rpc_size_ = group->make_event ("size");
    rpc_time_ = group->make_event ("time");
}

ServerHandlerImp::~ServerHandlerImp()
{
    m_server = nullptr;
}

void
ServerHandlerImp::setup (Setup const& setup, beast::Journal journal)
{
    setup_ = setup;
    m_server->ports (setup.ports);
}

//------------------------------------------------------------------------------

void
ServerHandlerImp::onStop()
{
    m_server->close();
}

//------------------------------------------------------------------------------

void
ServerHandlerImp::onAccept (HTTP::Session& session)
{
}

bool
ServerHandlerImp::onAccept (HTTP::Session& session,
    boost::asio::ip::tcp::endpoint endpoint)
{
    return true;
}

auto
ServerHandlerImp::onHandoff (HTTP::Session& session,
    std::unique_ptr <beast::asio::ssl_bundle>&& bundle,
        beast::http::message&& request,
            boost::asio::ip::tcp::endpoint remote_address) ->
    Handoff
{
    if (session.port().protocol.count("wss") > 0 &&
        isWebsocketUpgrade (request))
    {
        // Pass to websockets
        Handoff handoff;
        // handoff.moved = true;
        return handoff;
    }
    if (session.port().protocol.count("peer") > 0)
        return app_.overlay().onHandoff (std::move(bundle),
            std::move(request), remote_address);
    // Pass through to legacy onRequest
    return Handoff{};
}

auto
ServerHandlerImp::onHandoff (HTTP::Session& session,
    boost::asio::ip::tcp::socket&& socket,
        beast::http::message&& request,
            boost::asio::ip::tcp::endpoint remote_address) ->
    Handoff
{
    if (session.port().protocol.count("ws") > 0 &&
        isWebsocketUpgrade (request))
    {
        // Pass to websockets
        Handoff handoff;
        // handoff.moved = true;
        return handoff;
    }
    // Pass through to legacy onRequest
    return Handoff{};
}

static inline
Json::Output makeOutput (HTTP::Session& session)
{
    return [&](boost::string_ref const& b)
    {
        session.write (b.data(), b.size());
    };
}

void
ServerHandlerImp::onRequest (HTTP::Session& session)
{
    // Make sure RPC is enabled on the port
    if (session.port().protocol.count("http") == 0 &&
        session.port().protocol.count("https") == 0)
    {
        HTTPReply (403, "Forbidden", makeOutput (session));
        session.close (true);
        return;
    }

    // Check user/password authorization
    if (! authorized (
            session.port(), build_map(session.request().headers)))
    {
        HTTPReply (403, "Forbidden", makeOutput (session));
        session.close (true);
        return;
    }

    auto detach = session.detach();

    // We can copy `this` because ServerHandlerImp is a long-lasting singleton.
    auto job = [this, detach] (Job&) {
        RPC::runOnCoroutine(
            setup_.yieldStrategy.useCoroutines,
            [this, detach] (RPC::Suspend const& suspend) {
                processSession(detach, suspend);
            });
    };
    m_jobQueue.addJob(jtCLIENT, "RPC-Client", job);
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

//------------------------------------------------------------------------------

// Run as a couroutine.
void
ServerHandlerImp::processSession (
    std::shared_ptr<HTTP::Session> const& session, Suspend const& suspend)
{
    processRequest (
        session->port(),
        to_string (session->body()),
        session->remoteAddress().at_port (0),
        makeOutput (*session),
        suspend);

    if (session->request().keep_alive())
        session->complete();
    else
        session->close (true);
}

void
ServerHandlerImp::processRequest (
    HTTP::Port const& port,
    std::string const& request,
    beast::IP::Endpoint const& remoteIPAddress,
    Output&& output,
    Suspend const& suspend)
{
    // Move off the webserver thread onto the JobQueue.
    assert (app_.getJobQueue().getJobForThread());

    Json::Value jsonRPC;
    {
        Json::Reader reader;
        if ((request.size () > 1000000) ||
            ! reader.parse (request, jsonRPC) ||
            ! jsonRPC ||
            ! jsonRPC.isObject ())
        {
            HTTPReply (400, "Unable to parse request", output);
            return;
        }
    }

    // Parse id now so errors from here on will have the id
    //
    // VFALCO NOTE Except that "id" isn't included in the following errors.
    //
    Json::Value const& id = jsonRPC ["id"];
    Json::Value const& method = jsonRPC ["method"];

    if (! method) {
        HTTPReply (400, "Null method", output);
        return;
    }

    if (!method.isString ()) {
        HTTPReply (400, "method is not string", output);
        return;
    }

    /* ---------------------------------------------------------------------- */
    auto role = Role::FORBID;
    auto required = RPC::roleRequired(id.asString());

    if (jsonRPC.isObject() && jsonRPC.isMember("params") &&
            jsonRPC["params"].isArray() && jsonRPC["params"].size() > 0 &&
                jsonRPC["params"][Json::UInt(0)].isObject())
    {
        role = requestRole(required, port, jsonRPC["params"][Json::UInt(0)],
            remoteIPAddress);
    }
    else
    {
        role = requestRole(required, port, Json::objectValue,
            remoteIPAddress);
    }

    Resource::Consumer usage;

    if (role == Role::ADMIN)
        usage = m_resourceManager.newAdminEndpoint (
            remoteIPAddress.to_string());
    else
        usage = m_resourceManager.newInboundEndpoint(remoteIPAddress);

    if (usage.disconnect ())
    {
        HTTPReply (503, "Server is overloaded", output);
        return;
    }

    std::string strMethod = method.asString ();
    if (strMethod.empty())
    {
        HTTPReply (400, "method is empty", output);
        return;
    }

    // Extract request parameters from the request Json as `params`.
    //
    // If the field "params" is empty, `params` is an empty object.
    //
    // Otherwise, that field must be an array of length 1 (why?)
    // and we take that first entry and validate that it's an object.
    Json::Value params = jsonRPC [jss::params];

    if (! params)
        params = Json::Value (Json::objectValue);

    else if (!params.isArray () || params.size() != 1)
    {
        HTTPReply (400, "params unparseable", output);
        return;
    }
    else
    {
        params = std::move (params[0u]);
        if (!params.isObject())
        {
            HTTPReply (400, "params unparseable", output);
            return;
        }
    }

    // VFALCO TODO Shouldn't we handle this earlier?
    //
    if (role == Role::FORBID)
    {
        // VFALCO TODO Needs implementing
        // FIXME Needs implementing
        // XXX This needs rate limiting to prevent brute forcing password.
        HTTPReply (403, "Forbidden", output);
        return;
    }

    Resource::Charge loadType = Resource::feeReferenceRPC;

    m_journal.debug << "Query: " << strMethod << params;

    // Provide the JSON-RPC method as the field "command" in the request.
    params[jss::command] = strMethod;
    WriteLog (lsTRACE, RPCHandler)
        << "doRpcCommand:" << strMethod << ":" << params;

    auto const start (std::chrono::high_resolution_clock::now ());

    RPC::Context context {
        params, app_, loadType, m_networkOPs, app_.getLedgerMaster(), role,
        {app_, suspend, "RPC-Coroutine"}};

    std::string response;

    if (setup_.yieldStrategy.streaming == RPC::YieldStrategy::Streaming::yes)
    {
        executeRPC (context, response, setup_.yieldStrategy);
    }
    else
    {
        Json::Value result;
        RPC::doCommand (context, result, setup_.yieldStrategy);

        // Always report "status".  On an error report the request as received.
        if (result.isMember (jss::error))
        {
            result[jss::status] = jss::error;
            result[jss::request] = params;
            WriteLog (lsDEBUG, RPCErr) <<
                "rpcError: " << result [jss::error] <<
                ": " << result [jss::error_message];
        }
        else
        {
            result[jss::status]  = jss::success;
        }

        Json::Value reply (Json::objectValue);
        reply[jss::result] = std::move (result);
        response = to_string (reply);
    }

    rpc_time_.notify (static_cast <beast::insight::Event::value_type> (
        std::chrono::duration_cast <std::chrono::milliseconds> (
            std::chrono::high_resolution_clock::now () - start)));
    ++rpc_requests_;
    rpc_io_.notify (static_cast <beast::insight::Event::value_type> (
        context.metrics.fetches));
    rpc_size_.notify (static_cast <beast::insight::Event::value_type> (
        response.size ()));

    response += '\n';
    usage.charge (loadType);

    if (m_journal.debug.active())
    {
        static const int maxSize = 10000;
        if (response.size() <= maxSize)
            m_journal.debug << "Reply: " << response;
        else
            m_journal.debug << "Reply: " << response.substr (0, maxSize);
    }

    HTTPReply (200, response, output);
}

//------------------------------------------------------------------------------

// Returns `true` if the HTTP request is a Websockets Upgrade
// http://en.wikipedia.org/wiki/HTTP/1.1_Upgrade_header#Use_with_WebSockets
bool
ServerHandlerImp::isWebsocketUpgrade (beast::http::message const& request)
{
    if (request.upgrade())
        return request.headers["Upgrade"] == "websocket";
    return false;
}

// VFALCO TODO Rewrite to use beast::http::headers
bool
ServerHandlerImp::authorized (HTTP::Port const& port,
    std::map<std::string, std::string> const& h)
{
    if (port.user.empty() || port.password.empty())
        return true;

    auto const it = h.find ("authorization");
    if ((it == h.end ()) || (it->second.substr (0, 6) != "Basic "))
        return false;
    std::string strUserPass64 = it->second.substr (6);
    boost::trim (strUserPass64);
    std::string strUserPass = beast::base64_decode (strUserPass64);
    std::string::size_type nColon = strUserPass.find (":");
    if (nColon == std::string::npos)
        return false;
    std::string strUser = strUserPass.substr (0, nColon);
    std::string strPassword = strUserPass.substr (nColon + 1);
    return strUser == port.user && strPassword == port.password;
}

//------------------------------------------------------------------------------

void
ServerHandlerImp::onWrite (beast::PropertyStream::Map& map)
{
    m_server->onWrite (map);
}

//------------------------------------------------------------------------------

void
ServerHandler::appendStandardFields (beast::http::message& message)
{
}

//------------------------------------------------------------------------------

void
ServerHandler::Setup::makeContexts()
{
    for(auto& p : ports)
    {
        if (p.secure())
        {
            if (p.ssl_key.empty() && p.ssl_cert.empty() &&
                    p.ssl_chain.empty())
                p.context = make_SSLContext();
            else
                p.context = make_SSLContextAuthed (
                    p.ssl_key, p.ssl_cert, p.ssl_chain);
        }
        else
        {
            p.context = std::make_shared<
                boost::asio::ssl::context>(
                    boost::asio::ssl::context::sslv23);
        }
    }
}

namespace detail {

// Intermediate structure used for parsing
struct ParsedPort
{
    std::string name;
    std::set<std::string, beast::ci_less> protocol;
    std::string user;
    std::string password;
    std::string admin_user;
    std::string admin_password;
    std::string ssl_key;
    std::string ssl_cert;
    std::string ssl_chain;

    boost::optional<boost::asio::ip::address> ip;
    boost::optional<std::uint16_t> port;
    boost::optional<std::vector<beast::IP::Address>> admin_ip;
};

void
parse_Port (ParsedPort& port, Section const& section, std::ostream& log)
{
    {
        auto result = section.find("ip");
        if (result.second)
        {
            try
            {
                port.ip = boost::asio::ip::address::from_string(result.first);
            }
            catch(...)
            {
                log << "Invalid value '" << result.first <<
                    "' for key 'ip' in [" << section.name() << "]\n";
                throw std::exception();
            }
        }
    }

    {
        auto const result = section.find("port");
        if (result.second)
        {
            auto const ul = std::stoul(result.first);
            if (ul > std::numeric_limits<std::uint16_t>::max())
            {
                log << "Value '" << result.first
                    << "' for key 'port' is out of range\n";
                throw std::exception();
            }
            if (ul == 0)
            {
                log <<
                    "Value '0' for key 'port' is invalid\n";
                throw std::exception();
            }
            port.port = static_cast<std::uint16_t>(ul);
        }
    }

    {
        auto const result = section.find("protocol");
        if (result.second)
        {
            for (auto const& s : beast::rfc2616::split_commas(
                    result.first.begin(), result.first.end()))
                port.protocol.insert(s);
        }
    }

    {
        auto const result = section.find("admin");
        if (result.second)
        {
            std::stringstream ss (result.first);
            std::string ip;
            bool has_any (false);

            port.admin_ip.emplace ();
            while (std::getline (ss, ip, ','))
            {
                auto const addr = beast::IP::Endpoint::from_string_checked (ip);
                if (! addr.second)
                {
                    log << "Invalid value '" << ip << "' for key 'admin' in ["
                        << section.name () << "]\n";
                    throw std::exception ();
                }

                if (is_unspecified (addr.first))
                    has_any = true;

                if (has_any && ! port.admin_ip->empty ())
                {
                    log << "IP specified along with 0.0.0.0 '" << ip <<
                        "' for key 'admin' in [" << section.name () << "]\n";
                    throw std::exception ();
                }

                port.admin_ip->emplace_back (addr.first.address ());
            }
        }
    }

    set(port.user, "user", section);
    set(port.password, "password", section);
    set(port.admin_user, "admin_user", section);
    set(port.admin_password, "admin_password", section);
    set(port.ssl_key, "ssl_key", section);
    set(port.ssl_cert, "ssl_cert", section);
    set(port.ssl_chain, "ssl_chain", section);
}

HTTP::Port
to_Port(ParsedPort const& parsed, std::ostream& log)
{
    HTTP::Port p;
    p.name = parsed.name;

    if (! parsed.ip)
    {
        log << "Missing 'ip' in [" << p.name << "]\n";
        throw std::exception();
    }
    p.ip = *parsed.ip;

    if (! parsed.port)
    {
        log << "Missing 'port' in [" << p.name << "]\n";
        throw std::exception();
    }
    else if (*parsed.port == 0)
    {
        log << "Port " << *parsed.port << "in [" << p.name << "] is invalid\n";
        throw std::exception();
    }
    p.port = *parsed.port;
    if (parsed.admin_ip)
        p.admin_ip = *parsed.admin_ip;

    if (parsed.protocol.empty())
    {
        log << "Missing 'protocol' in [" << p.name << "]\n";
        throw std::exception();
    }
    p.protocol = parsed.protocol;
    if (p.websockets() &&
        (parsed.protocol.count("peer") > 0 ||
        parsed.protocol.count("http") > 0 ||
        parsed.protocol.count("https") > 0))
    {
        log << "Invalid protocol combination in [" << p.name << "]\n";
        throw std::exception();
    }

    p.user = parsed.user;
    p.password = parsed.password;
    p.admin_user = parsed.admin_user;
    p.admin_password = parsed.admin_password;
    p.ssl_key = parsed.ssl_key;
    p.ssl_cert = parsed.ssl_cert;
    p.ssl_chain = parsed.ssl_chain;

    return p;
}

std::vector<HTTP::Port>
parse_Ports (BasicConfig const& config, std::ostream& log)
{
    std::vector<HTTP::Port> result;

    if (! config.exists("server"))
    {
        log <<
            "Required section [server] is missing\n";
        throw std::exception();
    }

    ParsedPort common;
    parse_Port (common, config["server"], log);

    auto const& names = config.section("server").values();
    result.reserve(names.size());
    for (auto const& name : names)
    {
        if (! config.exists(name))
        {
            log <<
                "Missing section: [" << name << "]\n";
            throw std::exception();
        }
        ParsedPort parsed = common;
        parsed.name = name;
        parse_Port(parsed, config[name], log);
        result.push_back(to_Port(parsed, log));
    }

    std::size_t count = 0;
    for (auto const& p : result)
        if (p.protocol.count("peer") > 0)
            ++count;
    if (count > 1)
    {
        log << "Error: More than one peer protocol configured in [server]\n";
        throw std::exception();
    }
    if (count == 0)
        log << "Warning: No peer protocol configured\n";

    return result;
}

// Fill out the client portion of the Setup
void
setup_Client (ServerHandler::Setup& setup)
{
    decltype(setup.ports)::const_iterator iter;
    for (iter = setup.ports.cbegin();
            iter != setup.ports.cend(); ++iter)
        if (iter->protocol.count("http") > 0 ||
                iter->protocol.count("https") > 0)
            break;
    if (iter == setup.ports.cend())
        return;
    setup.client.secure =
        iter->protocol.count("https") > 0;
    setup.client.ip = iter->ip.to_string();
    // VFALCO HACK! to make localhost work
    if (setup.client.ip == "0.0.0.0")
        setup.client.ip = "127.0.0.1";
    setup.client.port = iter->port;
    setup.client.user = iter->user;
    setup.client.password = iter->password;
    setup.client.admin_user = iter->admin_user;
    setup.client.admin_password = iter->admin_password;
}

// Fill out the overlay portion of the Setup
void
setup_Overlay (ServerHandler::Setup& setup)
{
    auto const iter = std::find_if(setup.ports.cbegin(), setup.ports.cend(),
        [](HTTP::Port const& port)
        {
            return port.protocol.count("peer") > 0;
        });
    if (iter == setup.ports.cend())
    {
        setup.overlay.port = 0;
        return;
    }
    setup.overlay.ip = iter->ip;
    setup.overlay.port = iter->port;
}

}

ServerHandler::Setup
setup_ServerHandler(BasicConfig const& config, std::ostream& log)
{
    ServerHandler::Setup setup;
    setup.ports = detail::parse_Ports(config, log);
    setup.yieldStrategy = RPC::makeYieldStrategy(config);

    detail::setup_Client(setup);
    detail::setup_Overlay(setup);

    return setup;
}

std::unique_ptr <ServerHandler>
make_ServerHandler (Application& app, beast::Stoppable& parent,
    boost::asio::io_service& io_service, JobQueue& jobQueue,
        NetworkOPs& networkOPs, Resource::Manager& resourceManager,
            CollectorManager& cm)
{
    return std::make_unique<ServerHandlerImp>(app, parent,
        io_service, jobQueue, networkOPs, resourceManager, cm);
}

}
