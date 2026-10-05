#include <xrpld/rpc/ServerHandler.h>

#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/Overlay.h>
#include <xrpld/rpc/RPCHandler.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/detail/MaskSecrets.h>
#include <xrpld/rpc/detail/Tuning.h>
#include <xrpld/rpc/detail/WSInfoSub.h>

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
#include <xrpl/protocol/JsonRpc.h>
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
#include <cstddef>
#include <exception>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace xrpl {

class Peer;
class LedgerMaster;
class Transaction;
class ValidatorKeys;
class CanonicalTXSet;

static bool
isStatusRequest(HttpRequestType const& request)
{
    return request.version() >= 11 && request.target() == "/" && request.body().size() == 0 &&
        request.method() == boost::beast::http::verb::get;
}

static Handoff
statusRequestResponse(HttpRequestType const& request, boost::beast::http::status status)
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
 * Moves the notices a handler reported from its @p result to the top level of
 * the reply @p r.
 *
 * The rule is positional: a notice at the top level of the result is moved,
 * whatever it describes and whoever put it there, and one nested deeper stays
 * where it was put. The specification reserves no place for them, so they sit
 * beside its members either way, and a client reads one notice from one place.
 *
 * @param result The handler result to take the notices out of.
 * @param r The reply to write them to.
 */
static void
hoistNotices(json::Value& result, json::Value& r)
{
    for (auto const& name : {jss::warning, jss::warnings, jss::deprecated})
    {
        // One lookup per notice rather than two, `removeMember` answering null for a name the
        // result does not carry. No handler reports a notice set to null, which this would drop.
        if (json::Value notice = result.removeMember(name.cStr()); !notice.isNull())
            r[name] = std::move(notice);
    }
}

/**
 * The object a request presents its parameters in.
 *
 * `params` is an array holding one object for the by-position form and is that
 * object itself for the by-name form. Anything else reads as an empty object,
 * since a caller reads the version and the credentials out of it before the
 * shape is validated.
 *
 * Answers an Object or a Null and nothing else: Role.cpp reads `admin_password`
 * out of the result through the const `operator[]`, which asserts on any other
 * type, so widening this makes that line a remotely reachable assert.
 *
 * @param request The request, which need not be an object.
 * @return The parameters, or an empty object when the request presents none.
 *         Borrowed from @p request, so it lives as long as the request does.
 */
static json::Value const&
requestParams(json::Value const& request)
{
    static json::Value const kNoParams{json::ValueType::Object};
    if (!request.isObject())
        return kNoParams;

    auto const& params = request[jss::params];
    if (params.isArray() && params.size() > 0 && params[json::UInt(0)].isObjectOrNull())
        return params[json::UInt(0)];
    if (params.isObject())
        return params;

    return kNoParams;
}

/**
 * Reads the API version a JSON-RPC @p request asks for.
 *
 * The version lives with the request's parameters, where a client puts it, and
 * @p honorTopLevelFrom also accepts it beside the method.
 *
 * @param request The request to read.
 * @param honorTopLevelFrom The lowest version a value beside the method may
 *        select. kApiInvalidVersion honors any of them, including one the
 *        server cannot serve; kApiMinimumSpecVersion leaves such a request at
 *        version 1.
 * @param betaEnabled Whether the beta version counts as supported.
 * @return The version asked for. kApiInvalidVersion if the parameters name one
 *         the server cannot serve, or if the top level does and
 *         @p honorTopLevelFrom is kApiInvalidVersion. kApiVersionIfUnspecified
 *         if the request names none, and also if the only value is beside the
 *         method and below @p honorTopLevelFrom: a value the server cannot
 *         serve reads as kApiInvalidVersion, so it is below any other
 *         threshold, and such a request is answered as if it had named none,
 *         as it always has been.
 */
static unsigned
apiVersionOf(json::Value const& request, unsigned honorTopLevelFrom, bool betaEnabled)
{
    // Whichever object carries the member decides, so a request naming the version explicitly is
    // told apart from one naming none.
    auto const& params = requestParams(request);
    if (params.isMember(jss::api_version))
        return rpc::getAPIVersionNumber(params, betaEnabled);

    if (request.isMember(jss::api_version))
    {
        auto const version = rpc::getAPIVersionNumber(request, betaEnabled);
        if (version >= honorTopLevelFrom)
            return version;
    }

    return rpc::kApiVersionIfUnspecified;
}

/**
 * Whether @p request names an `id` the specification allows.
 *
 * The specification allows a string, a number or null there, and calls a
 * request naming anything else an invalid request. One naming no `id` at all is
 * allowed: every version answers such a request, and the reply names a null id.
 *
 * @param request The request being answered, which need not be an object.
 * @return true if the `id` is one the reply can echo.
 */
static bool
hasUsableSpecId(json::Value const& request)
{
    // isMember answers false for a request that is not an object, which names no id either.
    if (!request.isMember(jss::id))
        return true;

    json::Value const& id = request[jss::id];
    // Not isNumeric(), which defers to isIntegral() and so answers true for a boolean.
    return id.isNull() || id.isString() || id.isInt() || id.isUInt() || id.isDouble();
}

/**
 * The two JSON-RPC 2.0 members every reply carries, whether it reports a result
 * or a failure.
 *
 * `id` is read from the request's top level, where the specification puts it,
 * and is always present: the specification requires it either way, so a client
 * can always tell a response from a notification.
 *
 * An `id` the specification does not allow is answered with a null one rather
 * than echoed, since echoing it would answer with an object the specification
 * does not define. The check runs once the role is computed, since the charge
 * needs the `usage` that exists only then, and before the role and method
 * checks, so a client is told about its id whatever it asked for.
 *
 * @param request The request being answered, which need not be an object.
 * @return An object carrying `jsonrpc` and `id`, and nothing else.
 */
static json::Value
specEnvelope(json::Value const& request)
{
    json::Value r(json::ValueType::Object);
    r[jss::jsonrpc] = rpc::kJsonRpcVersion;
    r[jss::id] =
        hasUsableSpecId(request) && request.isMember(jss::id) ? request[jss::id] : json::Value();
    return r;
}

/**
 * A JSON-RPC 2.0 error response for a @p request rejected before it reached a
 * handler.
 *
 * Nothing the request carried is echoed: the specification makes `error.data`
 * optional, the client already knows what it sent, and copying a request per
 * rejection is what an overloaded server must not do.
 *
 * @param request The request being rejected.
 * @param code The class of failure, from JsonRpc.h.
 * @param message What the failure was.
 * @return The whole reply, ready to send.
 */
static json::Value
specError(json::Value const& request, json::Int code, std::string_view message)
{
    json::Value r = specEnvelope(request);

    json::Value err(json::ValueType::Object);
    err[jss::code] = code;
    err[jss::message] = message;
    r[jss::error] = std::move(err);

    return r;
}

/**
 * The reply to a request whose `id` the specification does not allow.
 *
 * It names no id at all: the id is the thing that was wrong, and the
 * specification asks for a null one wherever the id could not be read.
 *
 * @return The whole reply, ready to send.
 */
static json::Value
unusableIdRejection()
{
    return specError(
        json::Value(json::ValueType::Object),
        rpc::kJsonRpcInvalidRequest,
        "id is not a string, a number or null");
}

/**
 * Selects the member of a reported error that states what went wrong.
 *
 * `submit` and `simulate` put the specific detail in `error_exception` and
 * leave a generic `error_message` beside it, "Transaction is invalid." next
 * to "Transaction length invalid", so the more specific member is preferred
 * where both are present.
 *
 * @param reported An error a handler reported, which carries `error`. Only
 *        members it carries are read, so no read inserts a null one.
 * @return The member stating the failure, for the caller to move out of
 *         @p reported. Falls back to the error token.
 */
static json::Value&
errorMessageOf(json::Value& reported)
{
    if (reported.isMember(jss::error_exception))
        return reported[jss::error_exception];
    if (reported.isMember(jss::error_message))
        return reported[jss::error_message];
    return reported[jss::error];
}

/**
 * Shapes a handler @p result into a JSON-RPC 2.0 response object.
 *
 * An XRPL error is an application-level failure of a well-formed call, so it
 * reports the reserved implementation-defined code and carries its own token
 * and code in `data`. See kJsonRpcServerError. A method the server does not
 * have is the one exception: no command ran for it, and the specification
 * defines a code for that condition, so the token selects it.
 *
 * The specification's own members come from specEnvelope, so a reply and a
 * pre-dispatch rejection name the protocol and correlate with their request the
 * same way.
 *
 * @param result A handler result, consumed.
 * @param request The request being answered, where `id` is read.
 * @param r The reply this writes.
 * @param journal Where the error is logged.
 * @return The HTTP status the reply is sent with.
 */
static int
shapeSpecReply(
    json::Value result,
    json::Value const& request,
    json::Value& r,
    beast::Journal journal)
{
    r = specEnvelope(request);

    hoistNotices(result, r);

    if (!result.isMember(jss::error))
    {
        // `status` is dropped for the same reason it is on the error path below: which of `result`
        // or `error` is present already says whether the call succeeded. Only `path_find`'s status
        // subcommand sets it on a result, so it would otherwise survive there and nowhere else.
        result.removeMember(jss::status);
        r[jss::result] = std::move(result);
        return 200;
    }

    // Read through a const reference: `result`'s non-const `operator[]` inserts a null member for
    // an absent name, and the JLOG argument below is evaluated only when the journal is active.
    json::Value const& reported = result;

    JLOG(journal.debug()) << "rpcError: " << reported[jss::error] << ": "
                          << reported[jss::error_message];

    // Report the code that belongs to the token. Some handlers report a token whose own code they
    // cannot use, `error_code` being a value clients match on: the ledger_entry helpers name a
    // `malformed*` token per field and report `invalidParams` (31) for all of them. Those clients
    // are on an earlier version, so from version 3 the token and the code agree.
    json::Value data(json::ValueType::Object);
    json::Value const& reportedToken = reported[jss::error];
    data[jss::error] = reportedToken;

    // The token is read in place, `asString` copying it into a std::string once per error reply.
    // `codeForToken` answers RpcUnknown for the null pointer a string value can hold.
    char const* const tokenChars = reportedToken.isString() ? reportedToken.asCString() : nullptr;
    std::string_view const token{tokenChars != nullptr ? tokenChars : ""};

    auto code = rpc::codeForToken(token);
    if (code == RpcUnknown && reported[jss::error_code].isInt())
        code = static_cast<ErrorCodeI>(reported[jss::error_code].asInt());

    if (code != RpcUnknown)
        data[jss::error_code] = code;

    json::Value err(json::ValueType::Object);
    err[jss::code] =
        code == RpcUnknownCommand ? rpc::kJsonRpcMethodNotFound : rpc::kJsonRpcServerError;
    // Moved rather than copied: `result` is consumed, and the loop below skips the member.
    err[jss::message] = std::move(errorMessageOf(result));

    // Everything else the handler reported is detail about the failure, so it moves into `data`;
    // one member can be a whole partial result. The notices have already been hoisted out.
    for (auto it = result.begin(); it != result.end(); ++it)
    {
        // memberName() borrows the key; it.key() would build a Value and copy the string, per
        // member of every error reply.
        std::string_view const key{it.memberName()};
        if (key == jss::error.cStr() || key == jss::error_code.cStr() ||
            key == jss::error_message.cStr() || key == jss::error_exception.cStr() ||
            key == jss::status.cStr())
        {
            continue;
        }
        data[it.memberName()] = std::move(*it);
    }
    err[jss::data] = std::move(data);

    r[jss::error] = std::move(err);

    return code != RpcUnknown ? rpc::errorCodeHttpStatus(code) : 200;
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

            if ((setup_.overlay.port() == 0u) && port.protocol.contains("peer"))
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

    auto const c = [this, &port] {
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
    std::unique_ptr<StreamType>&& bundle,
    HttpRequestType&& request,
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

        std::shared_ptr<WSSession> ws;
        try
        {
            ws = session.websocketUpgrade();
        }
        catch (std::exception const& e)
        {
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
        // The only place that can account for an unreadable frame: it never reaches
        // processSession, where every other request is charged.
        auto const is = std::static_pointer_cast<WSInfoSub>(session->appDefined);
        is->getConsumer().charge(resource::kFeeMalformedRpc);
        if (is->getConsumer().disconnect(journal_))
        {
            session->close({boost::beast::websocket::policy_error, "threshold exceeded"});
            return;
        }

        // An unparsed body cannot be masked field-wise, so its size goes instead of its content.
        // Clamped rather than narrowed: json has no integer wider than 32 bits.
        json::Value jvResult(json::ValueType::Object);
        jvResult[jss::type] = jss::error;
        jvResult[jss::error] = "jsonInvalid";
        jvResult[jss::size] =
            json::UInt(std::min<std::size_t>(size, std::numeric_limits<json::UInt>::max()));
        boost::beast::multi_buffer sb;
        json::stream(jvResult, [&sb](auto const p, auto const n) {
            sb.commit(boost::asio::buffer_copy(sb.prepare(n), boost::asio::buffer(p, n)));
        });
        JLOG(journal_.trace()) << "Websocket sending '" << jvResult << "'";
        session->send(std::make_shared<StreambufWSMsg<decltype(sb)>>(std::move(sb)));
        session->complete();
        return;
    }

    JLOG(journal_.trace()) << "Websocket received '" << rpc::loggable(jv) << "'";

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

/**
 * Logs how long a request took.
 *
 * A slow request is reported at warn from one second and at error from ten,
 * with the duration only. The request is logged at debug, since it is client
 * text.
 *
 * @param request The request the duration belongs to.
 * @param duration How long processing it took.
 * @param journal Where the line is written.
 */
template <class T>
void
logDuration(json::Value const& request, T const& duration, beast::Journal& journal)
{
    using namespace std::chrono_literals;
    auto const micros = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();

    if (duration >= 1s)
    {
        auto const slow = duration >= 10s ? journal.error() : journal.warn();
        JLOG(slow) << "RPC request processing duration = " << micros << " microseconds.";
    }

    if (auto const stream = journal.debug())
    {
        stream << "RPC request processing duration = " << micros
               << " microseconds. request = " << rpc::loggable(request);
    }
}

json::Value
ServerHandler::processSession(
    std::shared_ptr<WSSession> const& session,
    std::shared_ptr<JobQueue::Coro> const& coro,
    json::Value const& jv)
{
    auto is = std::static_pointer_cast<WSInfoSub>(session->appDefined);
    if (is->getConsumer().disconnect(journal_))
    {
        session->close({boost::beast::websocket::policy_error, "threshold exceeded"});
        // FIX: This rpcError is not delivered since the session
        // was just closed.
        return rpcError(RpcSlowDown);
    }

    // Requests without "command" are invalid.
    json::Value jr(json::ValueType::Object);
    resource::Charge loadType = resource::kFeeReferenceRpc;
    // Needed after the try block as well, since it selects the reply envelope.
    auto const apiVersion = rpc::getAPIVersionNumber(jv, app_.config().betaRpcApi);

    // Marks a specification reply as a reply: a session also receives server-initiated messages,
    // and `type` is how a client tells the two apart.
    auto const asWsResponse = [](json::Value r) {
        r[jss::type] = jss::response;
        return r;
    };
    try
    {
        // The reason the message names no one method the server can look up, or nullptr when it
        // names one, a client being told which of the four it did rather than that one of them
        // happened. Below version 3 every one answers the single `missingCommand` token, which is
        // what a client on those versions matches on.
        auto const badMethod = [&] -> char const* {
            if (!jv.isMember(jss::command) && !jv.isMember(jss::method))
                return "Missing command entry.";
            if (jv.isMember(jss::command) && !jv[jss::command].isString())
                return "command is not a string";
            if (jv.isMember(jss::method) && !jv[jss::method].isString())
                return "method is not string";
            if (jv.isMember(jss::command) && jv.isMember(jss::method) &&
                jv[jss::command].asString() != jv[jss::method].asString())
            {
                return "command and method disagree";
            }
            return nullptr;
        }();

        // An `id` the specification does not allow is an invalid request, read before the method.
        // Earlier versions, and a version the server cannot serve, echo an id of any shape.
        if (rpc::isSpecVersion(apiVersion) && !hasUsableSpecId(jv))
        {
            is->getConsumer().charge(resource::kFeeMalformedRpc);
            return asWsResponse(unusableIdRejection());
        }

        if (apiVersion == rpc::kApiInvalidVersion || badMethod != nullptr)
        {
            is->getConsumer().charge(resource::kFeeMalformedRpc);

            // Each of these is an Invalid Request; kJsonRpcServerError is for a well-formed call
            // that failed. An unsupported version reads as 0 and takes the legacy shape: the server
            // cannot know which envelope that client speaks.
            if (badMethod != nullptr && rpc::isSpecVersion(apiVersion))
                return asWsResponse(specError(jv, rpc::kJsonRpcInvalidRequest, badMethod));

            jr[jss::type] = jss::response;
            jr[jss::status] = jss::error;
            jr[jss::error] = apiVersion == rpc::kApiInvalidVersion ? jss::invalid_API_version
                                                                   : jss::missingCommand;
            jr[jss::request] = rpc::maskSecrets(jv);
            if (jv.isMember(jss::id))
                jr[jss::id] = jv[jss::id];
            if (jv.isMember(jss::jsonrpc))
                jr[jss::jsonrpc] = jv[jss::jsonrpc];
            if (jv.isMember(jss::ripplerpc))
                jr[jss::ripplerpc] = jv[jss::ripplerpc];
            if (jv.isMember(jss::api_version))
                jr[jss::api_version] = jv[jss::api_version];

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

            // A refused role is a pre-dispatch rejection, so from version 3 it reports the
            // transport's code rather than an XRPL error inside the result. The charge is the one
            // the fall-through applies.
            if (rpc::isSpecVersion(apiVersion))
            {
                is->getConsumer().charge(loadType);
                return asWsResponse(specError(jv, rpc::kJsonRpcForbidden, "Forbidden"));
            }

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
        jr[jss::result] = rpc::makeError(RpcInternal);
        JLOG(journal_.error()) << "Exception while processing WS: " << ex.what() << "\n"
                               << "Input JSON: " << rpc::loggable(jv);
    }

    // From version 3 the notice goes inside the result, where shapeSpecReply hoists it to the top
    // level; below it stays beside the result.
    is->getConsumer().charge(loadType);
    if (is->getConsumer().warn())
    {
        auto& carrier = rpc::isSpecVersion(apiVersion) ? jr[jss::result] : jr;
        carrier[jss::warning] = jss::load;
    }

    // Shaped by the same helper the JSON-RPC transport uses, so a version selects one shape
    // whichever transport carries it. A WebSocket message arrives as a flat object rather than
    // nested under `params`, so the specification's `id` is read from that same object.
    if (rpc::isSpecVersion(apiVersion))
    {
        json::Value r(json::ValueType::Object);
        shapeSpecReply(std::move(jr[jss::result]), jv, r, journal_);
        return asWsResponse(std::move(r));
    }

    // Currently we will simply unwrap errors returned by the RPC
    // API, in the future maybe we can make the responses
    // consistent.
    //
    // Regularize result. This is duplicate code.
    if (jr[jss::result].isMember(jss::error))
    {
        jr = jr[jss::result];
        jr[jss::status] = jss::error;

        jr[jss::request] = rpc::maskSecrets(jv);
    }
    else
    {
        jr[jss::status] = jss::success;
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
    processRequest(
        session->port(),
        buffersToString(session->request().body().data()),
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

// Shape of the JSON-RPC reply envelope. The shape affects only how a completed request's result is
// wrapped for the wire; no request handler observes it.
//
// V1 through V3 are selected by the legacy `ripplerpc` request parameter. Spec is selected by
// `api_version` 3 and above, which takes precedence: a request naming both gets the spec envelope,
// and its `ripplerpc` selects nothing, so it is neither read nor checked. Below that version the
// field is validated, and a value naming no version is refused.
enum class RpcVersion {
    // Errors are returned under `result`, keyed by `error_message`, and echo the (secret-masked)
    // request. HTTP status is always 200.
    V1,
    // Errors are returned under `error`, keyed by `message`, and the request is not echoed. HTTP
    // status is always 200.
    V2,
    // As V2, but the error code selects a 4xx/5xx HTTP status.
    V3,
    // Conforms to the JSON-RPC 2.0 specification: `jsonrpc` and `id` members, the result under
    // `result` or the failure under `error` as {code, message, data}, and no `status`. The error
    // code selects a 4xx/5xx HTTP status.
    Spec,
};

constexpr RpcVersion kRpcVersionIfUnspecified = RpcVersion::V1;

/**
 * Maps a `ripplerpc` value onto the envelope version it selects.
 *
 * The value is unauthenticated, so anything but an exact match is refused.
 *
 * @param value The `ripplerpc` member, as sent.
 * @return The envelope version, or nullopt for the caller to reject.
 */
static std::optional<RpcVersion>
rpcVersion(std::string_view value)
{
    if (value == rpc::kRippleRpcVersion1)
        return RpcVersion::V1;
    if (value == rpc::kRippleRpcVersion2)
        return RpcVersion::V2;
    if (value == rpc::kRippleRpcVersion3)
        return RpcVersion::V3;
    return std::nullopt;
}

/**
 * The HTTP status the `ripplerpc: "3.0"` envelope reports for @p code.
 *
 * The codes below answer 200, which is what a client calling `account_info`
 * this way already reads. The list is closed: a code belongs on it only if it
 * already answered a coded reply at this status.
 *
 * @param code The error code the reply reports.
 * @return The HTTP status the reply is sent with.
 */
static int
legacyHttpStatus(ErrorCodeI code)
{
    switch (code)
    {
        case RpcActMalformed:
        case RpcActNotFound:
        case RpcAlreadyMultisig:
        case RpcAlreadySingleSig:
            return 200;
        default:
            return rpc::errorCodeHttpStatus(code);
    }
}

/**
 * Shapes a handler @p result into the reply envelope for @p version, appending
 * it to @p reply.
 *
 * @param version The envelope the reply takes.
 * @param result A handler result, consumed.
 * @param request The request's parameters, echoed on a version 1 error only,
 *        and where a legacy envelope reads `jsonrpc`, `ripplerpc` and `id`.
 * @param topLevel The object those parameters arrived in, where the
 *        specification envelope reads `id`.
 * @param batch Whether to append to @p reply rather than assign it.
 * @param reply The reply this writes.
 * @param journal Where an error is logged.
 * @return The HTTP status the reply is sent with. Versions 3 and Spec derive it
 *         from the error code, the others always reporting 200. Returned
 *         rather than read back off the reply, since only this function knows
 *         where in the shape it wrote the code. A batch discards it.
 */
static int
shapeReply(
    RpcVersion version,
    json::Value result,
    json::Value const& request,
    json::Value const& topLevel,
    bool batch,
    json::Value& reply,
    beast::Journal journal)
{
    json::Value r(json::ValueType::Object);
    int status = 200;

    if (version == RpcVersion::Spec)
    {
        status = shapeSpecReply(std::move(result), topLevel, r, journal);
    }
    else if (!result.isMember(jss::error))
    {
        result[jss::status] = jss::success;
        r[jss::result] = std::move(result);
    }
    else
    {
        // Read through a const reference: the non-const `operator[]` inserts a null member for an
        // absent name, and the reply below is built out of `result`.
        json::Value const& reported = result;

        JLOG(journal.debug()) << "rpcError: " << reported[jss::error] << ": "
                              << reported[jss::error_message];

        if (version == RpcVersion::V3 && reported[jss::error_code].isInt())
            status = legacyHttpStatus(static_cast<ErrorCodeI>(reported[jss::error_code].asInt()));

        result[jss::status] = jss::error;

        if (version == RpcVersion::V1)
        {
            result[jss::request] = rpc::maskSecrets(request);
            r[jss::result] = std::move(result);
        }
        else
        {
            result[jss::code] = result[jss::error_code];
            result[jss::message] = result[jss::error_message];
            result.removeMember(jss::error_message);
            r[jss::error] = std::move(result);
        }
    }

    // A legacy envelope echoes these from the request's parameters. The specification envelope
    // reads `jsonrpc` and `id` from the request itself, which specEnvelope has already done.
    if (version != RpcVersion::Spec)
    {
        if (request.isMember(jss::jsonrpc))
            r[jss::jsonrpc] = request[jss::jsonrpc];
        if (request.isMember(jss::ripplerpc))
            r[jss::ripplerpc] = request[jss::ripplerpc];
        if (request.isMember(jss::id))
            r[jss::id] = request[jss::id];
    }

    if (batch)
    {
        reply.append(std::move(r));
    }
    else
    {
        reply = std::move(r);
    }

    return status;
}

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
    auto rpcJ = app_.getJournal("RPC");

    // What a request presents when the server cannot read what it presents: an unparsable body or a
    // malformed entry carries no credentials to act on. A connection privileged by its address is
    // still privileged, `requestRole` reading the port rather than the request for that.
    static json::Value const kUnreadable(json::ValueType::Object);

    // The resource entry a request is charged against before its own role is read. `Role::GUEST` is
    // the requirement asked for, no method having been read that could ask for more.
    auto const usageFor = [&](json::Value const& presented) {
        return requestInboundEndpoint(
            resourceManager_,
            remoteIPAddress,
            requestRole(Role::GUEST, port, presented, remoteIPAddress, user),
            user,
            forwardedFor);
    };

    // Whether a charge also asks if the connection is over the drop threshold.
    enum class Threshold { Ignore, Ask };

    // Charges a request rejected before its own role is read, and reports whether the connection is
    // over the drop threshold. Only a caller that can act on the answer passes `Threshold::Ask`,
    // since asking is itself a charge: see chargeUnreadBody below.
    auto const chargeWithoutRole = [&](json::Value const& presented, Threshold threshold) {
        auto usage = usageFor(presented);
        usage.charge(resource::kFeeMalformedRpc);
        return threshold == Threshold::Ask && usage.disconnect(journal_);
    };

    // Charges a body the server rejects before it reads a request out of it. The drop threshold is
    // not asked about: `disconnect` is a mutator, charging the drop fee and counting a drop on
    // every call made while the balance is at or above the drop threshold, and this rejection
    // answers the whole body.
    auto const chargeUnreadBody = [&] { usageFor(kUnreadable).charge(resource::kFeeMalformedRpc); };

    json::Value jsonOrig;
    {
        // Only a parse failure can report the reader's reason, getFormattedErrorMessages being
        // built from what the reader recorded. The other three name their own cause.
        if (request.size() > rpc::tuning::kMaxRequestSize)
        {
            chargeUnreadBody();
            httpReply(400, "Request is too large", output, rpcJ);
            return;
        }

        json::Reader reader;
        if (!reader.parse(request, jsonOrig))
        {
            chargeUnreadBody();
            httpReply(
                400,
                "Unable to parse request: " + reader.getFormattedErrorMessages(),
                output,
                rpcJ);
            return;
        }

        if (!jsonOrig)
        {
            // A well-formed document that carries nothing: `{}`, `[]` or `null`.
            chargeUnreadBody();
            httpReply(400, "Request is empty", output, rpcJ);
            return;
        }

        if (!jsonOrig.isObject())
        {
            // A non-empty array, the only value the reader accepts that is neither null nor an
            // object. A number, string or boolean is a parse failure, answered above.
            chargeUnreadBody();
            httpReply(400, "Request is not a JSON object", output, rpcJ);
            return;
        }
    }

    bool batch = false;
    unsigned size = 1;
    // Spelled as a view so the name is compared in place. A bare literal would reach the
    // Value-to-Value comparison instead, which builds a Value from it, allocating per request.
    if (jsonOrig.isMember(jss::method) && jsonOrig[jss::method] == std::string_view{"batch"})
    {
        batch = true;
        if (!jsonOrig.isMember(jss::params) || !jsonOrig[jss::params].isArray())
        {
            chargeUnreadBody();
            httpReply(400, "Malformed batch request", output, rpcJ);
            return;
        }
        size = jsonOrig[jss::params].size();
    }

    json::Value reply(batch ? json::ValueType::Array : json::ValueType::Object);
    // Only a lone request selects the HTTP status: a batch may mix versions and reports each
    // entry's outcome in its own reply, so the batch itself always succeeds.
    int httpStatus = 200;
    auto const start(std::chrono::high_resolution_clock::now());
    for (unsigned i = 0; i < size; ++i)
    {
        json::Value const& jsonRPC = batch ? jsonOrig[jss::params][i] : jsonOrig;

        // Only an entry of a batch can be a non-object; a lone request was checked before the loop.
        // Inline rather than through `reject` below: an entry with no members carries the copy
        // under `request` whatever a caller asks for. It names no version either, so it is
        // answered in the legacy shape at every version.
        if (!jsonRPC.isObject())
        {
            // Only a batch reaches here, so the loop can act on the threshold. The entry presents
            // no credentials, so the connection pays.
            bool const overloaded = chargeWithoutRole(kUnreadable, Threshold::Ask);

            json::Value r(json::ValueType::Object);
            r[jss::request] = rpc::maskSecrets(jsonRPC);
            r[jss::error] = makeJsonError(rpc::kJsonRpcMethodNotFound, "Method not found");
            reply.append(std::move(r));

            if (overloaded)
                break;
            continue;
        }

        // A `method: "batch"` entry names its version beside its method, that form having no
        // parameters to nest inside. A lone request may spell it there too, but only a value naming
        // a specification version is honored: a top-level `api_version: 2` answers as version 1,
        // and honoring it would change a shipped reply shape.
        unsigned const apiVersion = apiVersionOf(
            jsonRPC,
            batch ? unsigned{rpc::kApiInvalidVersion} : unsigned{rpc::kApiMinimumSpecVersion},
            app_.config().betaRpcApi);

        // The request's own resource entry, assigned once its role is known below. A rejection
        // before that point charges through chargeWithoutRole.
        resource::Consumer usage;

        // For the two rejections that charge nothing here: an overloaded server sheds without
        // charging, disconnect() having counted the load already, and a version the server cannot
        // serve was charged before the role was known.
        constexpr resource::Charge const* kNoCharge = nullptr;

        // Answers a request rejected before it reached a handler, charging `fee` for it, and
        // reports whether the loop continues, which it does only for a batch: a lone request has
        // been answered in full. From API version 3 the answer is a specification error object that
        // echoes nothing; earlier versions answer a lone request with `message` as the whole body
        // and a batch entry with the error spliced into a copy of the entry, under `request` when
        // `wrapRequest` asks, reporting `legacyCode` where one is given.
        auto const reject = [&](int status,
                                json::Int code,
                                char const* message,
                                resource::Charge const* fee = & resource::kFeeMalformedRpc,
                                std::optional<json::Int> legacyCode = std::nullopt,
                                bool wrapRequest = false) {
            if (fee != nullptr)
                usage.charge(*fee);

            if (rpc::isSpecVersion(apiVersion))
            {
                if (!batch)
                {
                    httpReply(status, to_string(specError(jsonRPC, code, message)), output, rpcJ);
                    return false;
                }
                reply.append(specError(jsonRPC, code, message));
                return true;
            }

            if (!batch)
            {
                httpReply(status, message, output, rpcJ);
                return false;
            }

            json::Value r(json::ValueType::Object);
            if (wrapRequest)
            {
                r[jss::request] = rpc::maskSecrets(jsonRPC);
            }
            else
            {
                r = rpc::maskSecrets(jsonRPC);
            }
            r[jss::error] = makeJsonError(legacyCode.value_or(code), message);
            reply.append(std::move(r));
            return true;
        };

        if (apiVersion == rpc::kApiInvalidVersion)
        {
            bool const overloaded = chargeWithoutRole(
                requestParams(jsonRPC), batch ? Threshold::Ask : Threshold::Ignore);
            // An object-shaped rejection returns this entry under `request`, where a client
            // correlating by `reply[i].request` finds it.
            if (!reject(
                    400,
                    rpc::kJsonRpcWrongVersion,
                    jss::invalid_API_version.cStr(),
                    kNoCharge,
                    std::nullopt,
                    /*wrapRequest=*/true))
            {
                return;
            }
            if (overloaded)
                break;
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

        // The role comes from the credentials the request presents, which sit with its parameters
        // whichever form they take.
        role = requestRole(required, port, requestParams(jsonRPC), remoteIPAddress, user);

        usage = requestInboundEndpoint(resourceManager_, remoteIPAddress, role, user, forwardedFor);

        // An overloaded server sheds the request without charging for it; disconnect() has already
        // accounted for the load that got it here. The loop then stops: every later entry of the
        // same body would be shed too, and answering each one is itself work an overloaded server
        // must not do.
        if (!isUnlimited(role) && usage.disconnect(journal_))
        {
            if (!reject(503, rpc::kJsonRpcServerOverloaded, "Server is overloaded", kNoCharge))
                return;
            break;
        }

        // An `id` the specification does not allow makes this an invalid request. Answered here
        // rather than through `reject`, which would echo the id. Earlier versions echo an id of
        // any shape.
        if (rpc::isSpecVersion(apiVersion) && !hasUsableSpecId(jsonRPC))
        {
            usage.charge(resource::kFeeMalformedRpc);
            if (!batch)
            {
                httpReply(400, to_string(unusableIdRejection()), output, rpcJ);
                return;
            }
            reply.append(unusableIdRejection());
            continue;
        }

        if (role == Role::FORBID)
        {
            if (!reject(403, rpc::kJsonRpcForbidden, "Forbidden"))
                return;
            continue;
        }

        if (!jsonRPC.isMember(jss::method) || jsonRPC[jss::method].isNull())
        {
            if (!reject(
                    400,
                    rpc::kJsonRpcInvalidRequest,
                    "Null method",
                    &resource::kFeeMalformedRpc,
                    rpc::kJsonRpcMethodNotFound))
            {
                return;
            }
            continue;
        }

        json::Value const& method = jsonRPC[jss::method];
        if (!method.isString())
        {
            if (!reject(
                    400,
                    rpc::kJsonRpcInvalidRequest,
                    "method is not string",
                    &resource::kFeeMalformedRpc,
                    rpc::kJsonRpcMethodNotFound))
            {
                return;
            }
            continue;
        }

        std::string const strMethod = method.asString();
        if (strMethod.empty())
        {
            if (!reject(
                    400,
                    rpc::kJsonRpcInvalidRequest,
                    "method is empty",
                    &resource::kFeeMalformedRpc,
                    rpc::kJsonRpcMethodNotFound))
            {
                return;
            }
            continue;
        }

        // The `params` field carries the one object a handler reads, in either of the two forms the
        // specification defines: the object itself, or an array holding it.
        json::Value params;
        if (!batch)
        {
            params = jsonRPC[jss::params];
            if (!params)
            {
                params = json::Value(json::ValueType::Object);
            }
            else if (!params.isObject())
            {
                if (!params.isArray() || params.size() != 1)
                {
                    reject(400, rpc::kJsonRpcInvalidParams, "params unparsable");
                    return;
                }

                params = std::move(params[0u]);
                if (!params.isObjectOrNull())
                {
                    reject(400, rpc::kJsonRpcInvalidParams, "params unparsable");
                    return;
                }
            }
        }
        else  // batch
        {
            params = jsonRPC;
        }

        // Two methods name no one method to dispatch on. From version 3 the request is invalid;
        // earlier versions keep `unknownCmd`, at HTTP 200 unless the `ripplerpc: "3.0"` envelope
        // derives 405 from it.
        if (rpc::isSpecVersion(apiVersion) && params.isMember(jss::method) &&
            (!params[jss::method].isString() || params[jss::method].asString() != strMethod))
        {
            if (!reject(400, rpc::kJsonRpcInvalidRequest, "command and method disagree"))
                return;
            continue;
        }

        // `ripplerpc` selects nothing from version 3, so it is ignored rather than refused, as
        // every other field the server does not honor is.
        RpcVersion envelope = kRpcVersionIfUnspecified;
        if (rpc::isSpecVersion(apiVersion))
        {
            envelope = RpcVersion::Spec;
        }
        else if (params.isMember(jss::ripplerpc))
        {
            // A `ripplerpc` the server cannot honor is a bad parameter, so the second check reports
            // that code. The first keeps the method-not-found code shipped versions report.
            if (!params[jss::ripplerpc].isString())
            {
                if (!reject(400, rpc::kJsonRpcMethodNotFound, "ripplerpc is not a string"))
                    return;
                continue;
            }

            auto const parsed = rpcVersion(params[jss::ripplerpc].asString());
            if (!parsed)
            {
                if (!reject(
                        400, rpc::kJsonRpcInvalidParams, "ripplerpc is not a supported version"))
                    return;
                continue;
            }
            envelope = *parsed;
        }

        // Header-assigned values belong to the connection, so an entry not identified from a
        // secureGateway drops them for itself only rather than clearing them in place.
        bool const identified = role == Role::IDENTIFIED || role == Role::PROXY;
        std::string_view const entryForwardedFor = identified ? forwardedFor : std::string_view{};
        std::string_view const entryUser = identified ? user : std::string_view{};

        JLOG(journal_.debug()) << "Query: " << strMethod << rpc::loggable(params);

        // Provide the JSON-RPC method as the field "command" in the request.
        params[jss::command] = strMethod;
        JLOG(journal_.trace()) << "doRpcCommand:" << strMethod << ":" << rpc::loggable(params);

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
            {.user = entryUser, .forwardedFor = entryForwardedFor}};
        json::Value result;

        auto start = std::chrono::system_clock::now();

        try
        {
            rpc::doCommand(context, result);
        }
        catch (std::exception const& ex)
        {
            result = rpc::makeError(RpcInternal);
            JLOG(journal_.error()) << "Internal error : " << ex.what()
                                   << " when processing request: " << rpc::loggable(params);
        }

        auto end = std::chrono::system_clock::now();

        logDuration(params, end - start, journal_);

        usage.charge(loadType);
        if (usage.warn())
            result[jss::warning] = jss::load;

        int const status =
            shapeReply(envelope, std::move(result), params, jsonRPC, batch, reply, journal_);
        if (!batch)
            httpStatus = status;

        // A handler that returns its own `result` member leaves the legacy envelope with a doubly
        // nested one. The specification envelope has a fixed shape, so it is left alone.
        if (envelope != RpcVersion::Spec && reply.isMember(jss::result) &&
            reply[jss::result].isMember(jss::result))
        {
            reply = reply[jss::result];
            if (reply.isMember(jss::status))
            {
                reply[jss::result][jss::status] = reply[jss::status];
                reply.removeMember(jss::status);
            }
        }
    }

    auto response = to_string(reply);

    rpcTime_.notify(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - start));
    ++rpcRequests_;
    rpcSize_.notify(beast::insight::Event::value_type{response.size()});

    // The serialized reply is in hand, so it is logged as built; the masked copy and its second
    // serialization are paid for only when a credential has to be replaced.
    if (auto stream = journal_.debug())
    {
        if (rpc::hasSecret(reply))
        {
            stream << "Reply: " << rpc::loggable(reply);
        }
        else
        {
            stream << "Reply: " << std::string_view{response}.substr(0, rpc::kMaxLoggedChars);
        }
    }

    response += '\n';

    httpReply(httpStatus, response, output, rpcJ);
}

//------------------------------------------------------------------------------

/*  This response is used with load balancing.
    If the server is overloaded, status 500 is reported. Otherwise status 200
    is reported, meaning the server can accept more connections.
*/
Handoff
ServerHandler::statusResponse(HttpRequestType const& request) const
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
