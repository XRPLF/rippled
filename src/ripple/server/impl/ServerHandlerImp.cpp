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

#include <ripple/app/main/Application.h>
#include <ripple/server/make_ServerHandler.h>
#include <ripple/server/impl/JSONRPCUtil.h>
#include <ripple/server/impl/ServerHandlerImp.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/make_SSLContext.h>
#include <ripple/core/JobQueue.h>
#include <ripple/server/make_Server.h>
#include <ripple/overlay/Overlay.h>
#include <ripple/resource/Manager.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/Yield.h>
#include <beast/crypto/base64.h>
#include <beast/cxx14/algorithm.h> // <algorithm>
#include <boost/algorithm/string.hpp>
#include <boost/type_traits.hpp>
#include <boost/coroutine/all.hpp>
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

ServerHandlerImp::ServerHandlerImp (Stoppable& parent,
    boost::asio::io_service& io_service, JobQueue& jobQueue,
        NetworkOPs& networkOPs, Resource::Manager& resourceManager)
    : ServerHandler (parent)
    , m_resourceManager (resourceManager)
    , m_journal (deprecatedLogs().journal("Server"))
    , m_jobQueue (jobQueue)
    , m_networkOPs (networkOPs)
    , m_server (HTTP::make_Server(
        *this, io_service, deprecatedLogs().journal("Server")))
{
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

void
ServerHandlerImp::onLegacyPeerHello (
    std::unique_ptr<beast::asio::ssl_bundle>&& ssl_bundle,
        boost::asio::const_buffer buffer,
            boost::asio::ip::tcp::endpoint remote_address)
{
    // VFALCO TODO Inject Overlay
    getApp().overlay().onLegacyPeerHello(std::move(ssl_bundle),
        buffer, remote_address);
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
        return getApp().overlay().onHandoff (std::move(bundle),
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
RPC::Output makeOutput (HTTP::Session& session)
{
    return [&](RPC::Bytes const& b) { session.write (b.data, b.size); };
}

void
ServerHandlerImp::onRequest (HTTP::Session& session)
{
    // Check user/password authorization
    if (! authorized (session.port(),
        build_map(session.request().headers)))
    {
        HTTPReply (403, "Forbidden", makeOutput (session));
        session.close (true);
        return;
    }

    auto detach = session.detach();
    m_jobQueue.addJob (
        jtCLIENT, "RPC-Client",
        [detach, this] (Job& job) { processSession (job, detach); });
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

namespace {

template <typename CoroutinePtr>
void runCoroutineOnJobQueue (CoroutinePtr cp, JobQueue& jobQueue)
{
    auto& coroutine = *cp;
    if (!coroutine)
        return;
    coroutine();
    if (!coroutine)
        return;

    // Reschedule the job on the job queue.
    jobQueue.addJob (
        jtCLIENT, "RPC-Coroutine",
        [cp, &jobQueue] (Job& job) { runCoroutineOnJobQueue (cp, jobQueue); });
}

} // namespace

//------------------------------------------------------------------------------

// ServerHandlerImp will yield after emitting serverOutputChunkSize bytes.
// If this value is 0, it means "yield after each output"
// A negative value means "never yield"
// TODO(tom): negotiate a spot for this in Configs.
const int serverOutputChunkSize = -1;

// Dispatched on the job queue
void
ServerHandlerImp::processSession (
    Job&, std::shared_ptr<HTTP::Session> const& session)
{
    auto coroutine = std::make_shared<RPC::Coroutine> (
        RPC::yieldingCoroutine ([=] (Yield const& yield)
        {
            auto output = makeOutput (*session);
            if (serverOutputChunkSize >= 0)
            {
                output = RPC::chunkedYieldingOutput (
                    output, yield, serverOutputChunkSize);
            }

            processRequest (
                session->port(),
                to_string (session->body()),
                session->remoteAddress().at_port (0),
                output,
                yield);

            if (session->request().keep_alive())
                session->complete();
            else
                session->close (true);
        }));

    runCoroutineOnJobQueue (coroutine, m_jobQueue);
}

void
ServerHandlerImp::processRequest (
    HTTP::Port const& port,
    std::string const& request,
    beast::IP::Endpoint const& remoteIPAddress,
    Output output,
    Yield yield)
{
    Json::Value jsonRPC;
    {
        Json::Reader reader;
        if ((request.size () > 1000000) ||
            ! reader.parse (request, jsonRPC) ||
            jsonRPC.isNull () ||
            ! jsonRPC.isObject ())
        {
            HTTPReply (400, "Unable to parse request", output);
            return;
        }
    }

    auto const& admin_allow = getConfig().RPC_ADMIN_ALLOW;
    auto role = Role::FORBID;
    if (jsonRPC.isObject() && jsonRPC.isMember("params") &&
            jsonRPC["params"].isArray() && jsonRPC["params"].size() > 0 &&
                jsonRPC["params"][Json::UInt(0)].isObject())
        role = adminRole(port, jsonRPC["params"][Json::UInt(0)],
            remoteIPAddress, admin_allow);
    else
        role = adminRole(port, Json::objectValue,
            remoteIPAddress, admin_allow);

    Resource::Consumer usage;

    if (role == Role::ADMIN)
        usage = m_resourceManager.newAdminEndpoint (remoteIPAddress.to_string());
    else
        usage = m_resourceManager.newInboundEndpoint(remoteIPAddress);

    if (usage.disconnect ())
    {
        HTTPReply (503, "Server is overloaded", output);
        return;
    }

    // Parse id now so errors from here on will have the id
    //
    // VFALCO NOTE Except that "id" isn't included in the following errors.
    //
    Json::Value const id = jsonRPC ["id"];
    Json::Value const method = jsonRPC ["method"];

    if (method.isNull ())
    {
        HTTPReply (400, "Null method", output);
        return;
    }

    if (! method.isString ())
    {
        HTTPReply (400, "method is not string", output);
        return;
    }

    std::string strMethod = method.asString ();
    if (strMethod.empty())
    {
        HTTPReply (400, "method is empty", output);
        return;
    }

    // Parse params
    Json::Value params = jsonRPC ["params"];

    if (params.isNull ())
        params = Json::Value (Json::arrayValue);

    else if (!params.isArray ())
    {
        HTTPReply (400, "params unparseable", output);
        return;
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


    RPCHandler rpcHandler (m_networkOPs);
    Resource::Charge loadType = Resource::feeReferenceRPC;

    m_journal.debug << "Query: " << strMethod << params;

    auto result = rpcHandler.doRpcCommand (
        strMethod, params, role, loadType, yield);
    m_journal.debug << "Reply: " << result;

    usage.charge (loadType);

    Json::Value reply (Json::objectValue);
    reply[jss::result] = std::move (result);
    auto response = to_string (reply);
    response += '\n';

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

// Copied from Config::getAdminRole and modified to use the Port
Role
adminRole (HTTP::Port const& port,
    Json::Value const& params, beast::IP::Endpoint const& remoteIp)
{
    Role role (Role::FORBID);

    bool const bPasswordSupplied =
        params.isMember ("admin_user") ||
        params.isMember ("admin_password");

    bool const bPasswordRequired =
        ! port.admin_user.empty() || ! port.admin_password.empty();

    bool bPasswordWrong;

    if (bPasswordSupplied)
    {
        if (bPasswordRequired)
        {
            // Required, and supplied, check match
            bPasswordWrong =
                (port.admin_user !=
                    (params.isMember ("admin_user") ? params["admin_user"].asString () : ""))
                ||
                (port.admin_password !=
                    (params.isMember ("admin_user") ? params["admin_password"].asString () : ""));
        }
        else
        {
            // Not required, but supplied
            bPasswordWrong = false;
        }
    }
    else
    {
        // Required but not supplied,
        bPasswordWrong = bPasswordRequired;
    }

    // Meets IP restriction for admin.
    beast::IP::Endpoint const remote_addr (remoteIp.at_port (0));
    bool bAdminIP = false;

    for (auto const& allow_addr : getConfig().RPC_ADMIN_ALLOW)
    {
        if (allow_addr == remote_addr)
        {
            bAdminIP = true;
            break;
        }
    }

    if (bPasswordWrong                          // Wrong
            || (bPasswordSupplied && !bAdminIP))    // Supplied and doesn't meet IP filter.
    {
        role   = Role::FORBID;
    }
    // If supplied, password is correct.
    else
    {
        // Allow admin, if from admin IP and no password is required or it was supplied and correct.
        role = bAdminIP && (!bPasswordRequired || bPasswordSupplied) ? Role::ADMIN : Role::GUEST;
    }

    return role;
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

// Parse a comma-delimited list of values.
std::vector<std::string>
parse_csv (std::string const& in, std::ostream& log)
{
    auto first = in.cbegin();
    auto const last = in.cend();
    std::vector<std::string> result;
    if (first != last)
    {
        static boost::regex const re(
            "^"                         // start of line
            "(?:\\s*)"                  // whitespace (optional)
            "([a-zA-Z][_a-zA-Z0-9]*)"   // identifier
            "(?:\\s*)"                  // whitespace (optional)
            "(?:,?)"                    // comma (optional)
            "(?:\\s*)"                  // whitespace (optional)
            , boost::regex_constants::optimize
        );
        for(;;)
        {
            boost::smatch m;
            if (! boost::regex_search(first, last, m, re,
                boost::regex_constants::match_continuous))
            {
                log << "Expected <identifier>\n";
                throw std::exception();
            }
            result.push_back(m[1]);
            first = m[0].second;
            if (first == last)
                break;
        }
    }
    return result;
}

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
    boost::optional<bool> allow_admin;
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
                log <<
                    "Value '" << result.first << "' for key 'port' is out of range\n";
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
            for (auto const& s : parse_csv(result.first, log))
                port.protocol.insert(s);
        }
    }

    {
        auto const result = section.find("admin");
        if (result.second)
        {
            if (result.first == "no")
            {
                port.allow_admin = false;
            }
            else if (result.first == "allow")
            {
                port.allow_admin = true;
            }
            else
            {
                log << "Invalid value '" << result.first <<
                    "' for key 'admin' in [" << section.name() << "]\n";
                throw std::exception();
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

    if (! parsed.allow_admin)
        p.allow_admin = false;
    else
        p.allow_admin = *parsed.allow_admin;

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
setup_ServerHandler (BasicConfig const& config, std::ostream& log)
{
    ServerHandler::Setup setup;
    setup.ports = detail::parse_Ports (config, log);
    detail::setup_Client(setup);
    detail::setup_Overlay(setup);
    return setup;
}

std::unique_ptr <ServerHandler>
make_ServerHandler (beast::Stoppable& parent,
    boost::asio::io_service& io_service, JobQueue& jobQueue,
        NetworkOPs& networkOPs, Resource::Manager& resourceManager)
{
    return std::make_unique <ServerHandlerImp> (parent, io_service,
        jobQueue, networkOPs, resourceManager);
}

}
