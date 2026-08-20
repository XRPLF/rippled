#include <xrpld/rpc/ServerHandler.h>

#include <xrpld/app/main/Application.h>
#include <xrpld/core/Config.h>
#include <xrpld/overlay/Overlay.h>
#include <xrpld/rpc/RPCHandler.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/detail/Handler.h>
#include <xrpld/rpc/detail/RpcSpanNames.h>
#include <xrpld/rpc/detail/Tuning.h>
#include <xrpld/rpc/detail/WSInfoSub.h>
#include <xrpld/rpc/json_body.h>  // IWYU pragma: keep

#include <xrpl/basics/Log.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base64.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/make_SSLContext.h>
#include <xrpl/beast/net/IPAddress.h>
#include <xrpl/beast/net/IPAddressConversion.h>
#include <xrpl/beast/rfc2616.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/config/Constants.h>
#include <xrpl/core/Job.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/json/Output.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_reader.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/json_writer.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/ApiVersion.h>
#include <xrpl/protocol/BuildInfo.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Charge.h>
#include <xrpl/resource/Consumer.h>
#include <xrpl/resource/Fees.h>
#include <xrpl/resource/ResourceManager.h>
#include <xrpl/server/Handoff.h>
#include <xrpl/server/InfoSub.h>
#include <xrpl/server/NetworkOPs.h>
#include <xrpl/server/Port.h>
#include <xrpl/server/Server.h>
#include <xrpl/server/Session.h>
#include <xrpl/server/SimpleWriter.h>
#include <xrpl/server/WSSession.h>
#include <xrpl/server/detail/JSONRPCUtil.h>
#include <xrpl/telemetry/SpanGuard.h>

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/multi_buffer.hpp>
#include <boost/beast/http/fields.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/beast/websocket/impl/rfc6455.hpp>
#include <boost/beast/websocket/rfc6455.hpp>
#include <boost/system/detail/error_code.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <exception>
#include <map>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace xrpl {
using namespace telemetry;

class Peer;
class LedgerMaster;
class Transaction;
class ValidatorKeys;
class CanonicalTXSet;

static bool
isStatusRequest(http_request_type const& request)
{
    return request.version() >= 11 && request.target() == "/" && request.body().size() == 0 &&
        request.method() == boost::beast::http::verb::get;
}

static Handoff
statusRequestResponse(http_request_type const& request, boost::beast::http::status status)
{
    using namespace boost::beast::http;
    Handoff handoff;
    response<string_body> msg;
    msg.version(request.version());
    msg.result(status);
    msg.insert("Server", build_info::getFullVersionString());
    msg.insert("Content-Type", "text/html");
    msg.insert("Connection", "close");
    msg.body() = "Invalid protocol.";
    msg.prepare_payload();
    handoff.response = std::make_shared<SimpleWriter>(msg);
    return handoff;
}

/**
 * Resolve the command attribute for a WebSocket message to a bounded value.
 *
 * The command/method field is client-supplied and is promoted to a Prometheus
 * label by the spanmetrics connector, so the raw string must never be emitted:
 * arbitrary request input would drive unbounded label cardinality. Resolving
 * against the handler registry keeps per-command attribution for real commands
 * and collapses everything else to a single "unknown" series.
 *
 * A request naming both fields with different values is not a real command
 * (processSession rejects it below), so it also resolves to "unknown".
 *
 * @param jv      The parsed WebSocket request object.
 * @param config  Node config, for the beta-RPC-API flag used to look up the
 *                handler for the request's API version.
 * @return The canonical handler name, or "unknown" for an unrecognized,
 *         missing, non-string, or self-contradictory command.
 */
static std::string_view
resolveWsCommandSpanName(json::Value const& jv, Config const& config)
{
    bool const hasCommand = jv.isMember(jss::command) && jv[jss::command].isString();
    bool const hasMethod = jv.isMember(jss::method) && jv[jss::method].isString();
    if (!hasCommand && !hasMethod)
        return rpc_span::val::unknownCommand;

    std::string const cmd = hasCommand ? jv[jss::command].asString() : jv[jss::method].asString();
    if (hasCommand && hasMethod && cmd != jv[jss::method].asString())
        return rpc_span::val::unknownCommand;

    auto const* handler =
        rpc::getHandler(rpc::getAPIVersionNumber(jv, config.betaRpcApi), config.betaRpcApi, cmd);
    return (handler != nullptr) ? std::string_view{handler->name}
                                : std::string_view{rpc_span::val::unknownCommand};
}

// VFALCO TODO Rewrite to use boost::beast::http::fields
static bool
authorized(Port const& port, std::map<std::string, std::string> const& h)
{
    if (port.user.empty() || port.password.empty())
        return true;

    auto const it = h.find("authorization");
    if ((it == h.end()) || (!it->second.starts_with("Basic ")))
        return false;
    std::string strUserPass64 = it->second.substr(6);
    strUserPass64 = trimWhitespace(strUserPass64);
    std::string const strUserPass = base64Decode(strUserPass64);
    std::string::size_type const nColon = strUserPass.find(':');
    if (nColon == std::string::npos)
        return false;
    std::string const strUser = strUserPass.substr(0, nColon);
    std::string const strPassword = strUserPass.substr(nColon + 1);
    return strUser == port.user && strPassword == port.password;
}

ServerHandler::ServerHandler(
    ServerHandlerCreator const&,
    Application& app,
    boost::asio::io_context& ioContext,
    JobQueue& jobQueue,
    NetworkOPs& networkOPs,
    resource::Manager& resourceManager,
    CollectorManager& cm)
    : app_(app)
    , resourceManager_(resourceManager)
    , journal_(app_.getJournal("Server"))
    , networkOPs_(networkOPs)
    , server_(makeServer(*this, ioContext, app_.getJournal("Server")))
    , jobQueue_(jobQueue)
{
    auto const& group(cm.group("rpc"));
    rpcRequests_ = group->makeCounter("requests");
    rpcSize_ = group->makeEvent("size");
    rpcTime_ = group->makeEvent("time");
}

ServerHandler::~ServerHandler()
{
    server_ = nullptr;
}

void
ServerHandler::setup(Setup const& setup, beast::Journal journal)
{
    setup_ = setup;
    endpoints_ = server_->ports(setup.ports);

    // fix auto ports
    for (auto& port : setup_.ports)
    {
        if (auto it = endpoints_.find(port.name); it != endpoints_.end())
        {
            auto const endpointPort = it->second.port();
            if (port.port == 0u)
                port.port = endpointPort;

            if ((setup_.client.port == 0u) &&
                (port.protocol.contains("http") || port.protocol.contains("https")))
                setup_.client.port = endpointPort;

            if ((setup_.overlay.port() == 0u) && (port.protocol.contains("peer")))
                setup_.overlay.port(endpointPort);
        }
    }
}

//------------------------------------------------------------------------------

void
ServerHandler::stop()
{
    server_->close();
    {
        std::unique_lock lock(mutex_);
        condition_.wait(lock, [this] { return stopped_; });
    }
}

//------------------------------------------------------------------------------

bool
ServerHandler::onAccept(Session& session, boost::asio::ip::tcp::endpoint endpoint)
{
    auto const& port = session.port();

    auto const c = [this, &port]() {
        std::scoped_lock const lock(mutex_);
        return ++count_[port];
    }();

    if ((port.limit != 0) && c >= port.limit)
    {
        JLOG(journal_.trace()) << port.name << " is full; dropping " << endpoint;
        return false;
    }

    return true;
}

Handoff
ServerHandler::onHandoff(
    Session& session,
    std::unique_ptr<stream_type>&& bundle,
    http_request_type&& request,
    boost::asio::ip::tcp::endpoint const& remoteAddress)
{
    using namespace boost::beast;
    auto const& p{session.port().protocol};
    bool const isWs{
        p.contains("ws") || p.contains("ws2") || p.contains("wss") || p.contains("wss2")};

    if (websocket::is_upgrade(request))
    {
        if (!isWs)
            return statusRequestResponse(request, http::status::unauthorized);

        // Fresh root so each WS upgrade is its own trace, not nested under a
        // leaked ambient span on a reused coro worker.
        auto span = ScopedSpanGuard::freshRoot(
            TraceCategory::Rpc, rpc_span::prefix::rpc, rpc_span::op::wsUpgrade);
        std::shared_ptr<WSSession> ws;
        try
        {
            ws = session.websocketUpgrade();
            span.setOk();
        }
        catch (std::exception const& e)
        {
            span.recordException(e);  // LCOV_EXCL_LINE
            JLOG(journal_.error()) << "Exception upgrading websocket: " << e.what() << "\n";
            return statusRequestResponse(request, http::status::internal_server_error);
        }

        auto is{std::make_shared<WSInfoSub>(networkOPs_, ws)};
        auto const beastRemoteAddress = beast::IPAddressConversion::fromAsio(remoteAddress);
        is->getConsumer() = requestInboundEndpoint(
            resourceManager_,
            beastRemoteAddress,
            requestRole(Role::GUEST, session.port(), json::Value(), beastRemoteAddress, is->user()),
            is->user(),
            is->forwardedFor());
        ws->appDefined = std::move(is);
        ws->run();

        Handoff handoff;
        handoff.moved = true;
        return handoff;
    }

    if (bundle && p.contains("peer"))
        return app_.getOverlay().onHandoff(std::move(bundle), std::move(request), remoteAddress);

    if (isWs && isStatusRequest(request))
        return statusResponse(request);

    // Otherwise pass to legacy onRequest or websocket
    return {};
}

static inline json::Output
makeOutput(Session& session)
{
    return [&](std::string_view b) { session.write(b.data(), b.size()); };
}

static std::map<std::string, std::string>
buildMap(boost::beast::http::fields const& h)
{
    std::map<std::string, std::string> c;
    for (auto const& e : h)
    {
        // key cannot be a std::string_view because it needs to be used in
        // map and along with iterators
        std::string key(e.name_string());
        std::ranges::transform(
            key, key.begin(), [](auto kc) { return std::tolower(static_cast<unsigned char>(kc)); });
        c[key] = e.value();
    }
    return c;
}

template <class ConstBufferSequence>
static std::string
buffersToString(ConstBufferSequence const& bs)
{
    using boost::asio::buffer_size;
    std::string s;
    s.reserve(buffer_size(bs));
    // Use auto&& so the right thing happens whether bs returns a copy or
    // a reference
    for (auto&& b : bs)
        s.append(static_cast<char const*>(b.data()), buffer_size(b));
    return s;
}

void
ServerHandler::onRequest(Session& session)
{
    // Make sure RPC is enabled on the port
    if (!session.port().protocol.contains("http") && !session.port().protocol.contains("https"))
    {
        httpReply(403, "Forbidden", makeOutput(session), app_.getJournal("RPC"));
        session.close(true);
        return;
    }

    // Check user/password authorization
    if (!authorized(session.port(), buildMap(session.request())))
    {
        httpReply(403, "Forbidden", makeOutput(session), app_.getJournal("RPC"));
        session.close(true);
        return;
    }

    std::shared_ptr<Session> const detachedSession = session.detach();
    auto const postResult = jobQueue_.postCoro(
        JtClientRpc, "RPC-Client", [this, detachedSession](std::shared_ptr<JobQueue::Coro> coro) {
            processSession(detachedSession, coro);
        });
    if (postResult == nullptr)
    {
        // The coroutine was rejected, probably because we're shutting down.
        httpReply(503, "Service Unavailable", makeOutput(*detachedSession), app_.getJournal("RPC"));
        detachedSession->close(true);
        return;
    }
}

void
ServerHandler::onWSMessage(
    std::shared_ptr<WSSession> session,
    std::vector<boost::asio::const_buffer> const& buffers)
{
    json::Value jv;
    auto const size = boost::asio::buffer_size(buffers);
    if (size > rpc::tuning::kMaxRequestSize || !json::Reader{}.parse(jv, buffers) || !jv.isObject())
    {
        // Fresh root so each WS message is its own trace.
        auto span = ScopedSpanGuard::freshRoot(
            TraceCategory::Rpc, rpc_span::prefix::rpc, rpc_span::op::wsMessage);
        span.setError(rpc_span::val::invalidJson);

        json::Value jvResult(json::ValueType::Object);
        jvResult[jss::type] = jss::error;
        jvResult[jss::error] = "jsonInvalid";
        jvResult[jss::value] = ::xrpl::buffersToString(buffers);
        boost::beast::multi_buffer sb;
        json::stream(jvResult, [&sb](auto const p, auto const n) {
            sb.commit(boost::asio::buffer_copy(sb.prepare(n), boost::asio::buffer(p, n)));
        });
        JLOG(journal_.trace()) << "Websocket sending '" << jvResult << "'";
        session->send(std::make_shared<StreambufWSMsg<decltype(sb)>>(std::move(sb)));
        session->complete();
        return;
    }

    JLOG(journal_.trace()) << "Websocket received '" << jv << "'";

    auto const postResult = jobQueue_.postCoro(
        JtClientWebsocket,
        "WS-Client",
        [this, session, jv = std::move(jv)](std::shared_ptr<JobQueue::Coro> const& coro) {
            auto const jr = this->processSession(session, coro, jv);
            auto const s = to_string(jr);
            auto const n = s.length();
            boost::beast::multi_buffer sb(n);
            sb.commit(boost::asio::buffer_copy(sb.prepare(n), boost::asio::buffer(s.c_str(), n)));
            session->send(std::make_shared<StreambufWSMsg<decltype(sb)>>(std::move(sb)));
            session->complete();
        });
    if (postResult == nullptr)
    {
        // The coroutine was rejected, probably because we're shutting down.
        session->close({boost::beast::websocket::going_away, "Shutting Down"});
    }
}

void
ServerHandler::onClose(Session& session, boost::system::error_code const&)
{
    std::scoped_lock const lock(mutex_);
    --count_[session.port()];
}

void
ServerHandler::onStopped(Server&)
{
    std::scoped_lock const lock(mutex_);
    stopped_ = true;
    condition_.notify_one();
}

//------------------------------------------------------------------------------

template <class T>
void
logDuration(json::Value const& request, T const& duration, beast::Journal& journal)
{
    using namespace std::chrono_literals;
    auto const level = [&]() {
        if (duration >= 10s)
            return journal.error();
        if (duration >= 1s)
            return journal.warn();
        return journal.debug();
    }();

    JLOG(level) << "RPC request processing duration = "
                << std::chrono::duration_cast<std::chrono::microseconds>(duration).count()
                << " microseconds. request = " << request;
}

json::Value
ServerHandler::processSession(
    std::shared_ptr<WSSession> const& session,
    std::shared_ptr<JobQueue::Coro> const& coro,
    json::Value const& jv)
{
    // Fresh root so each WS message is its own trace.
    auto span = ScopedSpanGuard::freshRoot(
        TraceCategory::Rpc, rpc_span::prefix::rpc, rpc_span::op::wsMessage);
    // The command is client-supplied and becomes a Prometheus label via the
    // spanmetrics connector, so it is resolved against the handler registry
    // before emission: a recognized command keeps its canonical name, anything
    // else collapses to "unknown". Emitting the raw string would let request
    // input drive unbounded label cardinality. Mirrors the HTTP path's
    // resolveCommandSpanName().
    span.setAttribute(rpc_span::attr::command, resolveWsCommandSpanName(jv, app_.config()));

    auto is = std::static_pointer_cast<WSInfoSub>(session->appDefined);
    if (is->getConsumer().disconnect(journal_))
    {
        session->close({boost::beast::websocket::policy_error, "threshold exceeded"});
        // FIX: This rpcError is not delivered since the session
        // was just closed.
        span.setAttribute(rpc_span::attr::rpcStatus, rpc_span::val::error);
        span.setError("resource threshold exceeded");
        return rpcError(RpcSlowDown);
    }

    // Requests without "command" are invalid.
    json::Value jr(json::ValueType::Object);
    resource::Charge loadType = resource::kFeeReferenceRpc;
    try
    {
        auto apiVersion = rpc::getAPIVersionNumber(jv, app_.config().betaRpcApi);
        if (apiVersion == rpc::kApiInvalidVersion ||
            (!jv.isMember(jss::command) && !jv.isMember(jss::method)) ||
            (jv.isMember(jss::command) && !jv[jss::command].isString()) ||
            (jv.isMember(jss::method) && !jv[jss::method].isString()) ||
            (jv.isMember(jss::command) && jv.isMember(jss::method) &&
             jv[jss::command].asString() != jv[jss::method].asString()))
        {
            jr[jss::type] = jss::response;
            jr[jss::status] = jss::error;
            jr[jss::error] = apiVersion == rpc::kApiInvalidVersion ? jss::invalid_API_version
                                                                   : jss::missingCommand;
            jr[jss::request] = jv;
            if (jv.isMember(jss::id))
                jr[jss::id] = jv[jss::id];
            if (jv.isMember(jss::jsonrpc))
                jr[jss::jsonrpc] = jv[jss::jsonrpc];
            if (jv.isMember(jss::ripplerpc))
                jr[jss::ripplerpc] = jv[jss::ripplerpc];
            if (jv.isMember(jss::api_version))
                jr[jss::api_version] = jv[jss::api_version];

            is->getConsumer().charge(resource::kFeeMalformedRpc);
            span.setAttribute(rpc_span::attr::rpcStatus, rpc_span::val::error);
            span.setError(jr[jss::error].asString());
            return jr;
        }

        auto required = rpc::roleRequired(
            apiVersion,
            app_.config().betaRpcApi,
            jv.isMember(jss::command) ? jv[jss::command].asString() : jv[jss::method].asString());
        auto role = requestRole(
            required,
            session->port(),
            jv,
            beast::ip::fromAsio(session->remoteEndpoint().address()),
            is->user());
        if (Role::FORBID == role)
        {
            loadType = resource::kFeeMalformedRpc;
            jr[jss::result] = rpcError(RpcForbidden);
        }
        else
        {
            rpc::JsonContext context{
                {.j = app_.getJournal("RPCHandler"),
                 .app = app_,
                 .loadType = loadType,
                 .netOps = app_.getOPs(),
                 .ledgerMaster = app_.getLedgerMaster(),
                 .consumer = is->getConsumer(),
                 .role = role,
                 .coro = coro,
                 .infoSub = is,
                 .apiVersion = apiVersion},
                jv,
                {.user = is->user(), .forwardedFor = is->forwardedFor()}};

            auto start = std::chrono::system_clock::now();
            rpc::doCommand(context, jr[jss::result]);
            auto end = std::chrono::system_clock::now();
            logDuration(jv, end - start, journal_);
        }
    }
    catch (std::exception const& ex)
    {
        // LCOV_EXCL_START
        jr[jss::result] = rpc::makeError(RpcInternal);
        JLOG(journal_.error()) << "Exception while processing WS: " << ex.what() << "\n"
                               << "Input JSON: " << json::Compact{json::Value{jv}};
        span.recordException(ex);
        span.setAttribute(rpc_span::attr::rpcStatus, rpc_span::val::error);
        // LCOV_EXCL_STOP
    }

    is->getConsumer().charge(loadType);
    if (is->getConsumer().warn())
        jr[jss::warning] = jss::load;

    // Currently we will simply unwrap errors returned by the RPC
    // API, in the future maybe we can make the responses
    // consistent.
    //
    // Regularize result. This is duplicate code.
    if (jr[jss::result].isMember(jss::error))
    {
        jr = jr[jss::result];
        jr[jss::status] = jss::error;

        auto rq = jv;

        if (rq.isObject())
        {
            if (rq.isMember(jss::passphrase.cStr()))
                rq[jss::passphrase.cStr()] = "<masked>";
            if (rq.isMember(jss::secret.cStr()))
                rq[jss::secret.cStr()] = "<masked>";
            if (rq.isMember(jss::seed.cStr()))
                rq[jss::seed.cStr()] = "<masked>";
            if (rq.isMember(jss::seed_hex.cStr()))
                rq[jss::seed_hex.cStr()] = "<masked>";
        }

        jr[jss::request] = rq;
        // Mark the span according to the final result. Doing it here (rather
        // than an unconditional setOk later) ensures error responses — from
        // doCommand, a FORBID role, or the catch block above — are not
        // overwritten as OK.
        span.setAttribute(rpc_span::attr::rpcStatus, rpc_span::val::error);
        span.setError(jr[jss::error].asString());
    }
    else
    {
        if (jr[jss::result].isMember("forwarded") && jr[jss::result]["forwarded"])
            jr = jr[jss::result];
        jr[jss::status] = jss::success;
        span.setOk();
    }

    if (jv.isMember(jss::id))
        jr[jss::id] = jv[jss::id];
    if (jv.isMember(jss::jsonrpc))
        jr[jss::jsonrpc] = jv[jss::jsonrpc];
    if (jv.isMember(jss::ripplerpc))
        jr[jss::ripplerpc] = jv[jss::ripplerpc];
    if (jv.isMember(jss::api_version))
        jr[jss::api_version] = jv[jss::api_version];

    jr[jss::type] = jss::response;
    return jr;
}

// Run as a coroutine.
void
ServerHandler::processSession(
    std::shared_ptr<Session> const& session,
    std::shared_ptr<JobQueue::Coro> coro)
{
    // Fresh root so each HTTP request is its own trace, not nested under a
    // leaked ambient span on a reused coro worker.
    auto span = ScopedSpanGuard::freshRoot(
        TraceCategory::Rpc, rpc_span::prefix::rpc, rpc_span::op::httpRequest);

    auto const requestBody = ::xrpl::buffersToString(session->request().body().data());
    span.setAttribute(rpc_span::attr::requestPayloadSize, static_cast<int64_t>(requestBody.size()));

    processRequest(
        session->port(),
        requestBody,
        session->remoteAddress().atPort(0),
        makeOutput(*session),
        coro,
        forwardedFor(session->request()),
        [&] -> std::string_view {
            auto const iter = session->request().find("X-User");
            if (iter != session->request().end())
                return iter->value();
            return {};
        }());

    if (beast::rfc2616::isKeepAlive(session->request()))
    {
        session->complete();
    }
    else
    {
        session->close(true);
    }
    // Status is left unset: the OTel spec says instrumentation should leave it
    // unset unless the operation itself errored, and reserves Ok for an
    // operator asserting success. This span only delimits the HTTP request and
    // has no error of its own to report — the outcome is determined inside
    // rpc.process, which sets its own status.
}

static json::Value
makeJsonError(json::Int code, json::Value&& message)
{
    json::Value sub{json::ValueType::Object};
    sub["code"] = code;
    sub["message"] = std::move(message);
    json::Value r{json::ValueType::Object};
    r["error"] = sub;
    return r;
}

constexpr json::Int kMethodNotFound = -32601;
constexpr json::Int kServerOverloaded = -32604;
constexpr json::Int kForbidden = -32605;
constexpr json::Int kWrongVersion = -32606;

void
ServerHandler::processRequest(
    Port const& port,
    std::string const& request,
    beast::ip::Endpoint const& remoteIPAddress,
    Output const& output,
    std::shared_ptr<JobQueue::Coro> coro,
    std::string_view forwardedFor,
    std::string_view user)
{
    // Scoped child of rpc.http_request. Safe to hold across the coroutine
    // yield in doRipplePathFind: the coro-aware context storage moves this
    // scope with the coroutine on resume (it is never stranded on a worker's
    // thread-local stack), so nesting and log-trace correlation both hold.
    auto span = ScopedSpanGuard(TraceCategory::Rpc, rpc_span::prefix::rpc, rpc_span::op::process);
    auto rpcJ = app_.getJournal("RPC");

    // Tracks whether any failure occurred. Set on every error path (early
    // returns, the catch block, and per-request error replies) and used at the
    // end to mark the span status. The HTTP status code alone is insufficient:
    // it stays 200 for batch responses and for ripplerpc < 3.0, so relying on
    // it would let payload-level errors end the span as successful.
    bool spanHadError = false;

    // Marks the span as failed before sending an error reply, so the
    // early-return validation paths below are not later seen as successful
    // (the span would otherwise end UNSET, invisible to {status.code=error}).
    auto httpReplyError = [&](int status, std::string const& message) {
        spanHadError = true;
        span.setAttribute(rpc_span::attr::rpcStatus, rpc_span::val::error);
        span.setError(message);
        httpReply(status, message, output, rpcJ);
    };

    json::Value jsonOrig;
    {
        json::Reader reader;
        if ((request.size() > rpc::tuning::kMaxRequestSize) || !reader.parse(request, jsonOrig) ||
            !jsonOrig || !jsonOrig.isObject())
        {
            httpReplyError(400, "Unable to parse request: " + reader.getFormattedErrorMessages());
            return;
        }
    }

    bool batch = false;
    unsigned size = 1;
    if (jsonOrig.isMember(jss::method) && jsonOrig[jss::method] == "batch")
    {
        batch = true;
        if (!jsonOrig.isMember(jss::params) || !jsonOrig[jss::params].isArray())
        {
            httpReplyError(400, "Malformed batch request");
            return;
        }
        size = jsonOrig[jss::params].size();
    }
    span.setAttribute(rpc_span::attr::isBatch, batch);
    if (batch)
        span.setAttribute(rpc_span::attr::batchSize, static_cast<int64_t>(size));

    json::Value reply(batch ? json::ValueType::Array : json::ValueType::Object);

    // Append a per-request error item and record that the request failed.
    // Batch responses (and ripplerpc < 3.0) always carry HTTP 200, so the
    // span status can only learn about these failures through spanHadError.
    // Every per-item error path must go through here, or an entirely failed
    // batch would end its span as successful.
    auto appendItemError = [&](json::Value&& item) {
        spanHadError = true;
        reply.append(std::move(item));
    };

    auto const start(std::chrono::high_resolution_clock::now());
    for (unsigned i = 0; i < size; ++i)
    {
        json::Value const& jsonRPC = batch ? jsonOrig[jss::params][i] : jsonOrig;

        if (!jsonRPC.isObject())
        {
            json::Value r(json::ValueType::Object);
            r[jss::request] = jsonRPC;
            r[jss::error] = makeJsonError(kMethodNotFound, "Method not found");
            appendItemError(std::move(r));
            continue;
        }

        unsigned apiVersion = rpc::kApiVersionIfUnspecified;
        if (jsonRPC.isMember(jss::params) && jsonRPC[jss::params].isArray() &&
            jsonRPC[jss::params].size() > 0 && jsonRPC[jss::params][0u].isObject())
        {
            apiVersion = rpc::getAPIVersionNumber(
                jsonRPC[jss::params][json::UInt(0)], app_.config().betaRpcApi);
        }

        if (apiVersion == rpc::kApiVersionIfUnspecified && batch)
        {
            // for batch request, api_version may be at a different level
            apiVersion = rpc::getAPIVersionNumber(jsonRPC, app_.config().betaRpcApi);
        }

        if (apiVersion == rpc::kApiInvalidVersion)
        {
            if (!batch)
            {
                httpReplyError(400, jss::invalid_API_version.cStr());
                return;
            }
            json::Value r(json::ValueType::Object);
            r[jss::request] = jsonRPC;
            r[jss::error] = makeJsonError(kWrongVersion, jss::invalid_API_version.cStr());
            appendItemError(std::move(r));
            continue;
        }

        /* ------------------------------------------------------------------ */
        auto role = Role::FORBID;
        auto required = Role::FORBID;
        if (jsonRPC.isMember(jss::method) && jsonRPC[jss::method].isString())
        {
            required = rpc::roleRequired(
                apiVersion, app_.config().betaRpcApi, jsonRPC[jss::method].asString());
        }

        if (jsonRPC.isMember(jss::params) && jsonRPC[jss::params].isArray() &&
            jsonRPC[jss::params].size() > 0 && jsonRPC[jss::params][json::UInt(0)].isObjectOrNull())
        {
            role = requestRole(
                required, port, jsonRPC[jss::params][json::UInt(0)], remoteIPAddress, user);
        }
        else
        {
            role = requestRole(required, port, json::ValueType::Object, remoteIPAddress, user);
        }

        resource::Consumer usage;
        if (isUnlimited(role))
        {
            usage = resourceManager_.newUnlimitedEndpoint(remoteIPAddress);
        }
        else
        {
            usage = resourceManager_.newInboundEndpoint(
                remoteIPAddress, role == Role::PROXY, forwardedFor);
            if (usage.disconnect(journal_))
            {
                if (!batch)
                {
                    httpReplyError(503, "Server is overloaded");
                    return;
                }
                json::Value r = jsonRPC;
                r[jss::error] = makeJsonError(kServerOverloaded, "Server is overloaded");
                appendItemError(std::move(r));
                continue;
            }
        }

        if (role == Role::FORBID)
        {
            usage.charge(resource::kFeeMalformedRpc);
            if (!batch)
            {
                httpReplyError(403, "Forbidden");
                return;
            }
            json::Value r = jsonRPC;
            r[jss::error] = makeJsonError(kForbidden, "Forbidden");
            appendItemError(std::move(r));
            continue;
        }

        if (!jsonRPC.isMember(jss::method) || jsonRPC[jss::method].isNull())
        {
            usage.charge(resource::kFeeMalformedRpc);
            if (!batch)
            {
                httpReplyError(400, "Null method");
                return;
            }
            json::Value r = jsonRPC;
            r[jss::error] = makeJsonError(kMethodNotFound, "Null method");
            appendItemError(std::move(r));
            continue;
        }

        json::Value const& method = jsonRPC[jss::method];
        if (!method.isString())
        {
            usage.charge(resource::kFeeMalformedRpc);
            if (!batch)
            {
                httpReplyError(400, "method is not string");
                return;
            }
            json::Value r = jsonRPC;
            r[jss::error] = makeJsonError(kMethodNotFound, "method is not string");
            appendItemError(std::move(r));
            continue;
        }

        std::string const strMethod = method.asString();
        if (strMethod.empty())
        {
            usage.charge(resource::kFeeMalformedRpc);
            if (!batch)
            {
                httpReplyError(400, "method is empty");
                return;
            }
            json::Value r = jsonRPC;
            r[jss::error] = makeJsonError(kMethodNotFound, "method is empty");
            appendItemError(std::move(r));
            continue;
        }

        // Extract request parameters from the request Json as `params`.
        //
        // If the field "params" is empty, `params` is an empty object.
        //
        // Otherwise, that field must be an array of length 1 (why?)
        // and we take that first entry and validate that it's an object.
        json::Value params;
        if (!batch)
        {
            params = jsonRPC[jss::params];
            if (!params)
            {
                params = json::Value(json::ValueType::Object);
            }
            else if (!params.isArray() || params.size() != 1)
            {
                usage.charge(resource::kFeeMalformedRpc);
                httpReplyError(400, "params unparsable");
                return;
            }
            else
            {
                params = std::move(params[0u]);
                if (!params.isObjectOrNull())
                {
                    usage.charge(resource::kFeeMalformedRpc);
                    httpReplyError(400, "params unparsable");
                    return;
                }
            }
        }
        else  // batch
        {
            params = jsonRPC;
        }

        std::string ripplerpc = "1.0";
        if (params.isMember(jss::ripplerpc))
        {
            if (!params[jss::ripplerpc].isString())
            {
                usage.charge(resource::kFeeMalformedRpc);
                if (!batch)
                {
                    httpReplyError(400, "ripplerpc is not a string");
                    return;
                }

                json::Value r = jsonRPC;
                r[jss::error] = makeJsonError(kMethodNotFound, "ripplerpc is not a string");
                appendItemError(std::move(r));
                continue;
            }
            ripplerpc = params[jss::ripplerpc].asString();
        }

        /**
         * Clear header-assigned values if not positively identified from a
         * secureGateway.
         */
        if (role != Role::IDENTIFIED && role != Role::PROXY)
        {
            forwardedFor.remove_suffix(forwardedFor.size());
            user.remove_suffix(user.size());
        }

        JLOG(journal_.debug()) << "Query: " << strMethod << params;

        // Provide the JSON-RPC method as the field "command" in the request.
        params[jss::command] = strMethod;
        JLOG(journal_.trace()) << "doRpcCommand:" << strMethod << ":" << params;

        resource::Charge loadType = resource::kFeeReferenceRpc;

        rpc::JsonContext context{
            {.j = journal_,
             .app = app_,
             .loadType = loadType,
             .netOps = networkOPs_,
             .ledgerMaster = app_.getLedgerMaster(),
             .consumer = usage,
             .role = role,
             .coro = coro,
             .infoSub = InfoSub::pointer(),
             .apiVersion = apiVersion},
            params,
            {.user = user, .forwardedFor = forwardedFor}};
        json::Value result;

        auto start = std::chrono::system_clock::now();

        try
        {
            rpc::doCommand(context, result);
        }
        catch (std::exception const& ex)
        {
            // LCOV_EXCL_START
            result = rpc::makeError(RpcInternal);
            JLOG(journal_.error())
                << "Internal error : " << ex.what()
                << " when processing request: " << json::Compact{json::Value{params}};
            span.recordException(ex);
            span.setAttribute(rpc_span::attr::rpcStatus, rpc_span::val::error);
            spanHadError = true;
            // LCOV_EXCL_STOP
        }

        auto end = std::chrono::system_clock::now();

        logDuration(params, end - start, journal_);

        usage.charge(loadType);
        if (usage.warn())
            result[jss::warning] = jss::load;

        json::Value r(json::ValueType::Object);
        if (ripplerpc >= "2.0")
        {
            if (result.isMember(jss::error))
            {
                spanHadError = true;
                result[jss::status] = jss::error;
                result["code"] = result[jss::error_code];
                result["message"] = result[jss::error_message];
                result.removeMember(jss::error_message);
                JLOG(journal_.debug())
                    << "rpcError: " << result[jss::error] << ": " << result[jss::error_message];
                r[jss::error] = std::move(result);
            }
            else
            {
                result[jss::status] = jss::success;
                r[jss::result] = std::move(result);
            }
        }
        else
        {
            // Always report "status".  On an error report the request as
            // received.
            if (result.isMember(jss::error))
            {
                spanHadError = true;
                auto rq = params;

                if (rq.isObject())
                {  // But mask potentially sensitive information.
                    if (rq.isMember(jss::passphrase.cStr()))
                        rq[jss::passphrase.cStr()] = "<masked>";
                    if (rq.isMember(jss::secret.cStr()))
                        rq[jss::secret.cStr()] = "<masked>";
                    if (rq.isMember(jss::seed.cStr()))
                        rq[jss::seed.cStr()] = "<masked>";
                    if (rq.isMember(jss::seed_hex.cStr()))
                        rq[jss::seed_hex.cStr()] = "<masked>";
                }

                result[jss::status] = jss::error;
                result[jss::request] = rq;

                JLOG(journal_.debug())
                    << "rpcError: " << result[jss::error] << ": " << result[jss::error_message];
            }
            else
            {
                result[jss::status] = jss::success;
            }
            r[jss::result] = std::move(result);
        }

        if (params.isMember(jss::jsonrpc))
            r[jss::jsonrpc] = params[jss::jsonrpc];
        if (params.isMember(jss::ripplerpc))
            r[jss::ripplerpc] = params[jss::ripplerpc];
        if (params.isMember(jss::id))
            r[jss::id] = params[jss::id];
        if (batch)
        {
            reply.append(std::move(r));
        }
        else
        {
            reply = std::move(r);
        }

        if (reply.isMember(jss::result) && reply[jss::result].isMember(jss::result))
        {
            reply = reply[jss::result];
            if (reply.isMember(jss::status))
            {
                reply[jss::result][jss::status] = reply[jss::status];
                reply.removeMember(jss::status);
            }
        }
    }

    // If we're returning an error_code, use that to determine the HTTP status.
    int const httpStatus = [&reply]() {
        // This feature is enabled with ripplerpc version 3.0 and above.
        // Before ripplerpc version 3.0 always return 200.
        if (reply.isMember(jss::ripplerpc) && reply[jss::ripplerpc].isString() &&
            reply[jss::ripplerpc].asString() >= "3.0")
        {
            // If there's an error_code, use that to determine the HTTP Status.
            if (reply.isMember(jss::error) && reply[jss::error].isMember(jss::error_code) &&
                reply[jss::error][jss::error_code].isInt())
            {
                int const errCode = reply[jss::error][jss::error_code].asInt();
                return rpc::errorCodeHttpStatus(static_cast<ErrorCodeI>(errCode));
            }
        }
        // Return OK.
        return 200;
    }();

    auto response = to_string(reply);

    rpcTime_.notify(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - start));
    ++rpcRequests_;
    rpcSize_.notify(beast::insight::Event::value_type{response.size()});

    response += '\n';

    if (auto stream = journal_.debug())
    {
        static int const kMaxSize = 10000;
        if (response.size() <= kMaxSize)
        {
            stream << "Reply: " << response;
        }
        else
        {
            stream << "Reply: " << response.substr(0, kMaxSize);
        }
    }

    // Mark the span error if any request failed or the HTTP status is an error.
    // spanHadError catches payload-level errors that httpStatus misses (batch
    // responses and ripplerpc < 3.0 always return HTTP 200).
    if (spanHadError || httpStatus >= 400)
    {
        span.setAttribute(rpc_span::attr::rpcStatus, rpc_span::val::error);
        span.setError(rpc_span::val::error);
    }
    else
    {
        span.setOk();
    }
    httpReply(httpStatus, response, output, rpcJ);
}

//------------------------------------------------------------------------------

/*  This response is used with load balancing.
    If the server is overloaded, status 500 is reported. Otherwise status 200
    is reported, meaning the server can accept more connections.
*/
Handoff
ServerHandler::statusResponse(http_request_type const& request) const
{
    using namespace boost::beast::http;
    Handoff handoff;
    response<string_body> msg;
    std::string reason;
    if (app_.serverOkay(reason))
    {
        msg.result(boost::beast::http::status::ok);
        msg.body() = "<!DOCTYPE html><html><head><title>Test page for " + systemName() +
            "</title></head><body><h1>Test</h1><p>This page shows " + systemName() +
            " http(s) connectivity is working.</p></body></html>";
    }
    else
    {
        msg.result(boost::beast::http::status::internal_server_error);
        msg.body() = "<HTML><BODY>Server cannot accept clients: " + reason + "</BODY></HTML>";
    }
    msg.version(request.version());
    msg.insert("Server", build_info::getFullVersionString());
    msg.insert("Content-Type", "text/html");
    msg.insert("Connection", "close");
    msg.prepare_payload();
    handoff.response = std::make_shared<SimpleWriter>(msg);
    return handoff;
}

//------------------------------------------------------------------------------

void
ServerHandler::Setup::makeContexts()
{
    for (auto& p : ports)
    {
        if (p.secure())
        {
            if (p.sslKey.empty() && p.sslCert.empty() && p.sslChain.empty())
            {
                p.context = makeSslContext(p.sslCiphers);
            }
            else
            {
                p.context = makeSslContextAuthed(p.sslKey, p.sslCert, p.sslChain, p.sslCiphers);
            }
        }
        else
        {
            p.context =
                std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::sslv23);
        }
    }
}

static Port
toPort(ParsedPort const& parsed, std::ostream& log)
{
    Port p;
    p.name = parsed.name;

    if (!parsed.ip)
    {
        log << "Missing 'ip' in [" << p.name << "]";
        Throw<std::exception>();
    }
    p.ip = *parsed.ip;

    if (!parsed.port)
    {
        log << "Missing 'port' in [" << p.name << "]";
        Throw<std::exception>();
    }
    p.port = *parsed.port;

    if (parsed.protocol.empty())
    {
        log << "Missing 'protocol' in [" << p.name << "]";
        Throw<std::exception>();
    }
    p.protocol = parsed.protocol;

    p.user = parsed.user;
    p.password = parsed.password;
    p.adminUser = parsed.adminUser;
    p.adminPassword = parsed.adminPassword;
    p.sslKey = parsed.sslKey;
    p.sslCert = parsed.sslCert;
    p.sslChain = parsed.sslChain;
    p.sslCiphers = parsed.sslCiphers;
    p.pmdOptions = parsed.pmdOptions;
    p.wsQueueLimit = parsed.wsQueueLimit;
    p.limit = parsed.limit;
    p.adminNetsV4 = parsed.adminNetsV4;
    p.adminNetsV6 = parsed.adminNetsV6;
    p.secureGatewayNetsV4 = parsed.secureGatewayNetsV4;
    p.secureGatewayNetsV6 = parsed.secureGatewayNetsV6;

    return p;
}

static std::vector<Port>
parsePorts(Config const& config, std::ostream& log)
{
    std::vector<Port> result;

    if (!config.exists(Sections::kServer))
    {
        log << "Required section [server] is missing";
        Throw<std::exception>();
    }

    ParsedPort common;
    parsePort(common, config[Sections::kServer], log);

    auto const& names = config.section(Sections::kServer).values();
    result.reserve(names.size());
    for (auto const& name : names)
    {
        if (!config.exists(name))
        {
            log << "Missing section: [" << name << "]";
            Throw<std::exception>();
        }

        // grpc ports are parsed by GRPCServer class. Do not validate
        // grpc port information in this file.
        if (name == Sections::kPortGrpc)
            continue;

        ParsedPort parsed = common;
        parsePort(parsed, config[name], log);
        result.push_back(toPort(parsed, log));
    }

    if (config.standalone())
    {
        auto it = result.begin();

        while (it != result.end())
        {
            auto& p = it->protocol;

            // Remove the peer protocol, and if that would
            // leave the port empty, remove the port as well
            if ((p.erase("peer") != 0u) && p.empty())
            {
                it = result.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
    else
    {
        auto const count = std::ranges::count_if(
            result, [](Port const& p) { return p.protocol.contains("peer"); });

        if (count > 1)
        {
            log << "Error: More than one peer protocol configured in [server]";
            Throw<std::exception>();
        }

        if (count == 0)
            log << "Warning: No peer protocol configured";
    }

    return result;
}

// Fill out the client portion of the Setup
static void
setupClient(ServerHandler::Setup& setup)
{
    decltype(setup.ports)::const_iterator iter;
    for (iter = setup.ports.cbegin(); iter != setup.ports.cend(); ++iter)
    {
        if (iter->protocol.contains("http") || iter->protocol.contains("https"))
            break;
    }
    if (iter == setup.ports.cend())
        return;
    setup.client.secure = iter->protocol.contains("https");
    if (beast::ip::isUnspecified(iter->ip))
    {
        // VFALCO HACK! to make localhost work
        setup.client.ip = iter->ip.is_v6() ? "::1" : "127.0.0.1";
    }
    else
    {
        setup.client.ip = iter->ip.to_string();
    }
    setup.client.port = iter->port;
    setup.client.user = iter->user;
    setup.client.password = iter->password;
    setup.client.adminUser = iter->adminUser;
    setup.client.adminPassword = iter->adminPassword;
}

// Fill out the overlay portion of the Setup
static void
setupOverlay(ServerHandler::Setup& setup)
{
    auto const iter = std::ranges::find_if(
        setup.ports, [](Port const& port) { return port.protocol.contains("peer"); });
    if (iter == setup.ports.cend())
    {
        setup.overlay = {};
        return;
    }
    setup.overlay = {iter->ip, iter->port};
}

ServerHandler::Setup
setupServerHandler(Config const& config, std::ostream& log)
{
    ServerHandler::Setup setup;
    setup.ports = parsePorts(config, log);

    setupClient(setup);
    setupOverlay(setup);

    return setup;
}

std::unique_ptr<ServerHandler>
makeServerHandler(
    Application& app,
    boost::asio::io_context& ioContext,
    JobQueue& jobQueue,
    NetworkOPs& networkOPs,
    resource::Manager& resourceManager,
    CollectorManager& cm)
{
    return std::make_unique<ServerHandler>(
        ServerHandler::ServerHandlerCreator(),
        app,
        ioContext,
        jobQueue,
        networkOPs,
        resourceManager,
        cm);
}

}  // namespace xrpl
