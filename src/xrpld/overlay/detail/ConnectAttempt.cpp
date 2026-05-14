/** @file
 *  Implements the outbound peer connection state machine for the XRPL overlay.
 *
 *  `ConnectAttempt` drives a five-phase async pipeline — TCP connect, TLS
 *  handshake, HTTP upgrade write, HTTP upgrade read, and response processing —
 *  culminating in either promoting the result to a live `PeerImp` or reporting
 *  failure back to `PeerFinder`.  All async I/O is serialized on a strand; the
 *  dual-timer scheme (`timer_` global ceiling + `stepTimer_` per-phase budget)
 *  gives fine-grained diagnostics without separate handler functions.
 */
#include <xrpld/overlay/detail/ConnectAttempt.h>

#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/Cluster.h>
#include <xrpld/overlay/detail/Handshake.h>
#include <xrpld/overlay/detail/OverlayImpl.h>
#include <xrpld/overlay/detail/PeerImp.h>
#include <xrpld/overlay/detail/ProtocolVersion.h>
#include <xrpld/peerfinder/PeerfinderManager.h>
#include <xrpld/peerfinder/Slot.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/net/IPAddressConversion.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_reader.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/tokens.h>
#include <xrpl/resource/Consumer.h>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/ssl/stream_base.hpp>
#include <boost/asio/ssl/verify_mode.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core/stream_traits.hpp>
#include <boost/beast/http/impl/read.hpp>
#include <boost/beast/http/impl/write.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/system/system_error.hpp>

#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

namespace xrpl {

/** Construct a `ConnectAttempt` and bind it to its `PeerFinder` slot.
 *
 *  Initializes the TLS stream from the shared SSL context and wires
 *  convenience references (`socket_`, `stream_`) into `streamPtr_`.
 *  The slot is retained; it will be released to `PeerFinder::onClosed`
 *  by the destructor unless ownership is moved to a `PeerImp` on success.
 *  Call `run()` to begin the async pipeline.
 *
 *  @param app           Application context providing config and services.
 *  @param ioContext      Asio I/O context for all async operations.
 *  @param remoteEndpoint TCP endpoint of the peer to dial.
 *  @param usage          Resource consumer used for rate-limiting this peer.
 *  @param context        Shared SSL context (TLS version, ciphers, etc.).
 *  @param id             Unique numeric identifier for this connection attempt.
 *  @param slot           PeerFinder slot that was reserved for this outbound
 *      connection; must not be null.
 *  @param journal        Journal sink for diagnostic logging.
 *  @param overlay        Parent overlay manager that owns this child.
 */
ConnectAttempt::ConnectAttempt(
    Application& app,
    boost::asio::io_context& ioContext,
    endpoint_type remoteEndpoint,
    Resource::Consumer usage,
    shared_context const& context,
    std::uint32_t id,
    std::shared_ptr<PeerFinder::Slot> const& slot,
    beast::Journal journal,
    OverlayImpl& overlay)
    : Child(overlay)
    , app_(app)
    , id_(id)
    , sink_(journal, OverlayImpl::makePrefix(id))
    , journal_(sink_)
    , remoteEndpoint_(std::move(remoteEndpoint))
    , usage_(usage)
    , strand_(boost::asio::make_strand(ioContext))
    , timer_(ioContext)
    , stepTimer_(ioContext)
    , streamPtr_(
          std::make_unique<stream_type>(
              socket_type(std::forward<boost::asio::io_context&>(ioContext)),
              *context))
    , socket_(streamPtr_->next_layer().socket())
    , stream_(*streamPtr_)
    , slot_(slot)
{
}

/** Release the PeerFinder slot if the connection never succeeded.
 *
 *  On a successful connection, `processResponse()` moves `slot_` into the
 *  newly created `PeerImp`, leaving `slot_` null here.  Any other outcome
 *  (failure, timeout, early stop) leaves `slot_` non-null, so the destructor
 *  reports the closure to `PeerFinder` to keep its bookkeeping consistent.
 */
ConnectAttempt::~ConnectAttempt()
{
    if (slot_ != nullptr)
        overlay_.peerFinder().onClosed(slot_);
}

/** Abort the connection attempt from any thread.
 *
 *  If called from outside the strand, the call is re-posted to the strand
 *  so that `shutdown()` always executes with strand ownership.  Idempotent
 *  when the socket is already closed.
 */
void
ConnectAttempt::stop()
{
    if (!strand_.running_in_this_thread())
    {
        boost::asio::post(strand_, std::bind(&ConnectAttempt::stop, shared_from_this()));
        return;
    }

    if (!socket_.is_open())
        return;

    JLOG(journal_.debug()) << "stop: Stop";

    shutdown();
}

/** Begin the async connection pipeline from any thread.
 *
 *  Re-posts to the strand if necessary, then arms both the global timeout
 *  and the `TcpConnect` step timer before issuing the async TCP connect.
 *  `onConnect()` continues the pipeline on success.
 */
void
ConnectAttempt::run()
{
    if (!strand_.running_in_this_thread())
    {
        boost::asio::post(strand_, std::bind(&ConnectAttempt::run, shared_from_this()));
        return;
    }

    JLOG(journal_.debug()) << "run: connecting to " << remoteEndpoint_;

    ioPending_ = true;

    setTimer(ConnectionStep::TcpConnect);

    stream_.next_layer().async_connect(
        remoteEndpoint_,
        boost::asio::bind_executor(
            strand_,
            std::bind(&ConnectAttempt::onConnect, shared_from_this(), std::placeholders::_1)));
}

//------------------------------------------------------------------------------

/** Mark the connection as shutting down and begin teardown.
 *
 *  Sets `shutdown_` to prevent further protocol steps, cancels all pending
 *  async I/O on the lowest-layer socket, then delegates to
 *  `tryAsyncShutdown()` which will either start the SSL shutdown handshake
 *  or close the socket directly, depending on how far the TLS handshake
 *  progressed.
 *
 *  @note Must be called from the strand.  Idempotent when the socket is
 *      already closed.
 */
void
ConnectAttempt::shutdown()
{
    XRPL_ASSERT(
        strand_.running_in_this_thread(), "xrpl::ConnectAttempt::shutdown: strand in this thread");

    if (!socket_.is_open())
        return;

    shutdown_ = true;
    boost::beast::get_lowest_layer(stream_).cancel();

    tryAsyncShutdown();
}

/** Initiate the SSL shutdown handshake when it is safe to do so.
 *
 *  This method is a no-op in three situations: shutdown has not been
 *  requested yet, shutdown is already in progress, or an async I/O
 *  operation is still in flight (`ioPending_`).  The last guard prevents
 *  calling `stream_.async_shutdown()` while a concurrent `async_read` or
 *  `async_write` is outstanding, which would be undefined behavior.
 *
 *  If the TLS handshake never completed (the connection is still in the
 *  `TcpConnect` or `TlsHandshake` phase) there is no TLS session to
 *  close, so `close()` is called directly instead.
 *
 *  @note Must be called from the strand.
 */
void
ConnectAttempt::tryAsyncShutdown()
{
    XRPL_ASSERT(
        strand_.running_in_this_thread(),
        "xrpl::ConnectAttempt::tryAsyncShutdown : strand in this thread");

    if (!shutdown_ || currentStep_ == ConnectionStep::ShutdownStarted)
        return;

    if (ioPending_)
        return;

    if (currentStep_ != ConnectionStep::TcpConnect && currentStep_ != ConnectionStep::TlsHandshake)
    {
        setTimer(ConnectionStep::ShutdownStarted);
        stream_.async_shutdown(bind_executor(
            strand_,
            std::bind(&ConnectAttempt::onShutdown, shared_from_this(), std::placeholders::_1)));
        return;
    }

    close();
}

/** Handle completion of the async TLS shutdown handshake.
 *
 *  Several error codes are expected during connection teardown and are
 *  intentionally suppressed to avoid log noise:
 *  - `eof` — the stream was cleanly closed by the peer.
 *  - `operation_aborted` — the step timer expired before the shutdown
 *    handshake could complete.
 *  - `stream_truncated` — the TCP connection dropped before the TLS close
 *    notify exchange finished (benign if the peer doesn't do graceful
 *    disconnect).
 *  - `"application data after close notify"` — a benign OpenSSL condition
 *    triggered by some peers sending application data after the close notify.
 *
 *  Any other error is logged at debug level.  In all cases `close()` is
 *  called to finalize socket teardown.
 *
 *  @param ec Error code from the async TLS shutdown operation.
 */
void
ConnectAttempt::onShutdown(error_code ec)
{
    cancelTimer();

    if (ec)
    {
        bool const shouldLog =
            (ec != boost::asio::error::eof && ec != boost::asio::error::operation_aborted &&
             ec.message().find("application data after close notify") == std::string::npos);

        if (shouldLog)
        {
            JLOG(journal_.debug()) << "onShutdown: " << ec.message();
        }
    }

    close();
}

/** Cancel all timers and close the underlying TCP socket.
 *
 *  This is the terminal cleanup step, called after either a successful TLS
 *  shutdown or when bypassing the TLS shutdown (pre-handshake teardown).
 *  The error code from `socket_.close()` is deliberately discarded — the
 *  connection is being torn down regardless of any OS-level close error.
 *
 *  @note Must be called from the strand.  Idempotent when the socket is
 *      already closed.
 */
void
ConnectAttempt::close()
{
    XRPL_ASSERT(
        strand_.running_in_this_thread(), "xrpl::ConnectAttempt::close : strand in this thread");
    if (!socket_.is_open())
        return;

    cancelTimer();

    error_code ec;
    socket_.close(ec);  // NOLINT(bugprone-unused-return-value)
}

/** Log a failure reason at debug level and begin connection teardown.
 *
 *  @param reason Human-readable description of why the connection failed.
 */
void
ConnectAttempt::fail(std::string const& reason)
{
    JLOG(journal_.debug()) << reason;
    shutdown();
}

/** Log a system error at debug level and begin connection teardown.
 *
 *  @param name Context label prepended to the error message (e.g. "onConnect").
 *  @param ec   The system error code describing the failure.
 */
void
ConnectAttempt::fail(std::string const& name, error_code ec)
{
    JLOG(journal_.debug()) << name << ": " << ec.message();
    shutdown();
}

/** Advance the current connection step and arm the appropriate timers.
 *
 *  Two timers are maintained:
 *  - **Global timer** (`timer_`): armed once for the entire attempt
 *    (`kCONNECT_TIMEOUT = 25 s`).  Subsequent calls to this method are
 *    no-ops for the global timer (guarded by checking against the
 *    default-constructed `time_point{}`).
 *  - **Step timer** (`stepTimer_`): reset on every call via
 *    `expires_after`, which automatically cancels the previous step timer.
 *    Phase-specific budgets: TcpConnect 8 s, TlsHandshake 8 s,
 *    HttpWrite 3 s, HttpRead 3 s, ShutdownStarted 2 s.
 *
 *  Both timers share `onTimer()` as their handler; the handler
 *  distinguishes which fired by comparing their expiry against
 *  `steady_clock::now()`.
 *
 *  `Init` and `Complete` steps do not arm a step timer.  On any exception
 *  from the Asio timer API, `close()` is called immediately.
 *
 *  @param step The phase being entered; updates `currentStep_`.
 */
void
ConnectAttempt::setTimer(ConnectionStep step)
{
    currentStep_ = step;

    if (timer_.expiry() == std::chrono::steady_clock::time_point{})
    {
        try
        {
            timer_.expires_after(kCONNECT_TIMEOUT);
            timer_.async_wait(
                boost::asio::bind_executor(
                    strand_,
                    std::bind(
                        &ConnectAttempt::onTimer, shared_from_this(), std::placeholders::_1)));
        }
        catch (std::exception const& ex)
        {
            JLOG(journal_.error()) << "setTimer (global): " << ex.what();
            close();
            return;
        }
    }

    try
    {
        std::chrono::seconds stepTimeout;
        switch (step)
        {
            case ConnectionStep::TcpConnect:
                stepTimeout = StepTimeouts::kTCP_CONNECT;
                break;
            case ConnectionStep::TlsHandshake:
                stepTimeout = StepTimeouts::kTLS_HANDSHAKE;
                break;
            case ConnectionStep::HttpWrite:
                stepTimeout = StepTimeouts::kHTTP_WRITE;
                break;
            case ConnectionStep::HttpRead:
                stepTimeout = StepTimeouts::kHTTP_READ;
                break;
            case ConnectionStep::ShutdownStarted:
                stepTimeout = StepTimeouts::kTLS_SHUTDOWN;
                break;
            case ConnectionStep::Complete:
            case ConnectionStep::Init:
                return;
        }

        // expires_after implicitly cancels the previous step timer.
        stepTimer_.expires_after(stepTimeout);
        stepTimer_.async_wait(
            boost::asio::bind_executor(
                strand_,
                std::bind(&ConnectAttempt::onTimer, shared_from_this(), std::placeholders::_1)));

        JLOG(journal_.trace()) << "setTimer: " << stepToString(step)
                               << " timeout=" << stepTimeout.count() << "s";
    }
    catch (std::exception const& ex)
    {
        JLOG(journal_.error()) << "setTimer (step " << stepToString(step) << "): " << ex.what();
        close();
        return;
    }
}

/** Cancel both the global timer and the current step timer.
 *
 *  Any `system_error` thrown by `cancel()` is swallowed — timers that are
 *  already expired or closed produce benign errors that must not propagate.
 */
void
ConnectAttempt::cancelTimer()
{
    try
    {
        timer_.cancel();
        stepTimer_.cancel();
    }
    catch (boost::system::system_error const&)  // NOLINT(bugprone-empty-catch)
    {
        // ignored
    }
}

/** Shared handler for both the global and step timers.
 *
 *  `operation_aborted` is returned whenever a timer is cancelled (e.g. by
 *  `expires_after` or `cancelTimer()`); this is expected and ignored.  Any
 *  other non-zero error code is unexpected and triggers an immediate
 *  `close()`.
 *
 *  When the error code is zero (a genuine expiry), both timers' expiry
 *  times are compared against `steady_clock::now()` to determine which
 *  one fired — the global ceiling (`timer_`) or the per-step budget
 *  (`stepTimer_`).  Either way the connection is closed; the distinction
 *  exists only for logging granularity.
 *
 *  @param ec Error code from the Asio timer wait operation.
 */
void
ConnectAttempt::onTimer(error_code ec)
{
    if (!socket_.is_open())
        return;

    if (ec)
    {
        // Timers are frequently cancelled (step transition, cleanup); do not
        // treat operation_aborted as a failure.
        if (ec == boost::asio::error::operation_aborted)
            return;

        // This should never happen
        JLOG(journal_.error()) << "onTimer: " << ec.message();
        close();
        return;
    }

    auto const now = std::chrono::steady_clock::now();
    bool const globalExpired = (timer_.expiry() <= now);
    bool const stepExpired = (stepTimer_.expiry() <= now);

    if (globalExpired)
    {
        JLOG(journal_.debug()) << "onTimer: Global timeout; step: " << stepToString(currentStep_);
    }
    else if (stepExpired)
    {
        JLOG(journal_.debug()) << "onTimer: Step timeout; step: " << stepToString(currentStep_);
    }
    else
    {
        JLOG(journal_.warn()) << "onTimer: Unexpected timer callback";
    }

    close();
}

/** Handle completion of the async TCP connect.
 *
 *  Clears `ioPending_` first, then checks the error code.
 *  `operation_aborted` means a timer fired while the connect was in flight;
 *  in that case shutdown is already in progress and `tryAsyncShutdown()` is
 *  called to continue teardown cleanly.
 *
 *  After a nominal connect, `local_endpoint()` is queried to confirm the OS
 *  actually established a route (the connect can succeed at the Asio level
 *  with a closed socket in rare edge cases).  TLS certificate verification
 *  is explicitly disabled (`verify_none`); security derives from the
 *  node-key signature in the HTTP handshake, not from the cert chain.
 *
 *  @param ec Error code from the async TCP connect.
 */
void
ConnectAttempt::onConnect(error_code ec)
{
    ioPending_ = false;

    if (ec)
    {
        if (ec == boost::asio::error::operation_aborted)
        {
            tryAsyncShutdown();
            return;
        }

        fail("onConnect", ec);
        return;
    }

    if (!socket_.is_open())
        return;

    // Confirm the route is live; a closed socket can slip through on some OSes.
    socket_.local_endpoint(ec);
    if (ec)
    {
        fail("onConnect", ec);
        return;
    }

    if (shutdown_)
    {
        tryAsyncShutdown();
        return;
    }

    ioPending_ = true;

    setTimer(ConnectionStep::TlsHandshake);

    stream_.set_verify_mode(boost::asio::ssl::verify_none);
    stream_.async_handshake(
        boost::asio::ssl::stream_base::client,
        boost::asio::bind_executor(
            strand_,
            std::bind(&ConnectAttempt::onHandshake, shared_from_this(), std::placeholders::_1)));
}

/** Handle completion of the async TLS handshake.
 *
 *  After a successful TLS handshake this method:
 *  1. Confirms the connection is not a self-loop via
 *     `peerFinder().onConnected()`.
 *  2. Derives the TLS-channel-bound shared value with `makeSharedValue()`;
 *     a degenerate zero-XOR result causes an immediate shutdown (logged
 *     inside `makeSharedValue`).
 *  3. Builds the HTTP upgrade request with `makeRequest()` and populates
 *     identity headers with `buildHandshake()`.
 *  4. Starts the async HTTP write to send the upgrade to the peer.
 *
 *  @param ec Error code from the async TLS handshake.
 */
void
ConnectAttempt::onHandshake(error_code ec)
{
    ioPending_ = false;

    if (ec)
    {
        if (ec == boost::asio::error::operation_aborted)
        {
            tryAsyncShutdown();
            return;
        }

        fail("onHandshake", ec);
        return;
    }

    auto const localEndpoint = socket_.local_endpoint(ec);
    if (ec)
    {
        fail("onHandshake", ec);
        return;
    }

    setTimer(ConnectionStep::HttpWrite);

    if (!overlay_.peerFinder().onConnected(
            slot_, beast::IPAddressConversion::fromAsio(localEndpoint)))
    {
        fail("Self connection");
        return;
    }

    auto const sharedValue = makeSharedValue(*streamPtr_, journal_);
    if (!sharedValue)
    {
        shutdown();
        return;  // makeSharedValue logs
    }

    req_ = makeRequest(
        !overlay_.peerFinder().config().peerPrivate,
        app_.config().COMPRESSION,
        app_.config().LEDGER_REPLAY,
        app_.config().TX_REDUCE_RELAY_ENABLE,
        app_.config().VP_REDUCE_RELAY_BASE_SQUELCH_ENABLE);

    buildHandshake(
        req_,
        *sharedValue,
        overlay_.setup().networkID,
        overlay_.setup().publicIp,
        remoteEndpoint_.address(),
        app_);

    if (shutdown_)
    {
        tryAsyncShutdown();
        return;
    }

    ioPending_ = true;

    boost::beast::http::async_write(
        stream_,
        req_,
        boost::asio::bind_executor(
            strand_,
            std::bind(&ConnectAttempt::onWrite, shared_from_this(), std::placeholders::_1)));
}

/** Handle completion of the async HTTP upgrade request write.
 *
 *  On success, arms the `HttpRead` step timer and starts the async read of
 *  the peer's HTTP upgrade response into `response_`.
 *
 *  @param ec Error code from the async HTTP write.
 */
void
ConnectAttempt::onWrite(error_code ec)
{
    ioPending_ = false;

    if (ec)
    {
        if (ec == boost::asio::error::operation_aborted)
        {
            tryAsyncShutdown();
            return;
        }

        fail("onWrite", ec);
        return;
    }

    if (shutdown_)
    {
        tryAsyncShutdown();
        return;
    }

    ioPending_ = true;

    setTimer(ConnectionStep::HttpRead);

    boost::beast::http::async_read(
        stream_,
        readBuf_,
        response_,
        boost::asio::bind_executor(
            strand_,
            std::bind(&ConnectAttempt::onRead, shared_from_this(), std::placeholders::_1)));
}

/** Handle completion of the async HTTP upgrade response read.
 *
 *  Cancels all timers and advances `currentStep_` to `Complete` before
 *  inspecting the error code.  An `eof` here means the peer closed the
 *  connection before sending a full HTTP response, which is treated as a
 *  soft failure (logged at debug rather than warning).  On success,
 *  delegates to `processResponse()` to validate the peer's identity and
 *  promote the connection.
 *
 *  @param ec Error code from the async HTTP read.
 */
void
ConnectAttempt::onRead(error_code ec)
{
    cancelTimer();
    ioPending_ = false;
    currentStep_ = ConnectionStep::Complete;

    if (ec)
    {
        if (ec == boost::asio::error::eof)
        {
            JLOG(journal_.debug()) << "EOF";
            shutdown();
            return;
        }

        if (ec == boost::asio::error::operation_aborted)
        {
            tryAsyncShutdown();
            return;
        }

        fail("onRead", ec);
        return;
    }

    if (shutdown_)
    {
        tryAsyncShutdown();
        return;
    }

    processResponse();
}

//--------------------------------------------------------------------------

/** Validate the peer's HTTP upgrade response and promote to a live `PeerImp`.
 *
 *  This is the security-critical terminal step of the connection pipeline.
 *  Two response paths are handled:
 *
 *  **HTTP 101 (Switching Protocols)** — the normal success path:
 *  1. The `Upgrade` header is parsed for exactly one protocol version that
 *     is also locally supported via `isProtocolSupported()`; ambiguous or
 *     unsupported negotiations are rejected.
 *  2. `makeSharedValue()` re-derives the TLS channel-bound value; a
 *     degenerate zero-XOR result triggers an immediate shutdown.
 *  3. `verifyHandshake()` validates the peer's node-key signature over the
 *     shared value, checks `Network-ID`, `Network-Time`, and IP headers.
 *     Any failure throws `std::runtime_error`, caught below.
 *  4. `peerFinder().activate()` confirms the slot is still acceptable
 *     (duplicate-key check, slot-count limits).
 *  5. `streamPtr_` and `slot_` are moved into a new `PeerImp`; after the
 *     move both are null here, so the destructor skips `onClosed`.
 *     `overlay_.addActive()` registers the live peer.
 *
 *  **HTTP 503 (Service Unavailable)** — the redirect path:
 *  A 503 with a JSON body containing a `"peer-ips"` array is the XRPL
 *  redirect mechanism: the peer is overloaded and provides alternative
 *  addresses.  Valid endpoints are forwarded to
 *  `peerFinder().onRedirects()` for future connection attempts.  A 503
 *  without a valid redirect body (e.g. a plain HTTP proxy error) is
 *  logged as a warning and the connection is torn down.  Either way this
 *  `ConnectAttempt` is terminated after handling.
 *
 *  @note `verifyHandshake()` throws on any check failure; callers of this
 *      method do not need to check a return value — failure is always
 *      exception-based.
 */
void
ConnectAttempt::processResponse()
{
    if (!OverlayImpl::isPeerUpgrade(response_))
    {
        if (response_.result() != boost::beast::http::status::service_unavailable)
        {
            JLOG(journal_.warn()) << "Unable to upgrade to peer protocol: " << response_.result()
                                  << " (" << response_.reason() << ")";
            shutdown();
            return;
        }

        std::string responseBody;
        responseBody.reserve(boost::asio::buffer_size(response_.body().data()));
        for (auto const buffer : response_.body().data())
        {
            responseBody.append(
                static_cast<char const*>(buffer.data()), boost::asio::buffer_size(buffer));
        }

        json::Value json;
        json::Reader reader;
        auto const isValidJson = reader.parse(responseBody, json);

        auto const isRedirect = isValidJson && json.isObject() && json.isMember("peer-ips");

        if (!isRedirect)
        {
            JLOG(journal_.warn()) << "processResponse: " << remoteEndpoint_
                                  << " failed to upgrade to peer protocol: " << response_.result()
                                  << " (" << response_.reason() << ")";

            shutdown();
            return;
        }

        json::Value const& peerIps = json["peer-ips"];
        if (!peerIps.isArray())
        {
            fail("processResponse: invalid peer-ips format");
            return;
        }

        std::vector<boost::asio::ip::tcp::endpoint> redirectEndpoints;
        redirectEndpoints.reserve(peerIps.size());

        for (auto const& ipValue : peerIps)
        {
            if (!ipValue.isString())
                continue;

            error_code ec;
            auto const endpoint = parseEndpoint(ipValue.asString(), ec);
            if (!ec)
                redirectEndpoints.push_back(endpoint);
        }

        // redirectEndpoints may be empty if all entries were malformed.
        overlay_.peerFinder().onRedirects(remoteEndpoint_, redirectEndpoints);

        fail("processResponse: failed to connect to peer: redirected");
        return;
    }

    // The peer echoes back the protocol version it selected; verify it is
    // also acceptable to us (exactly one version, locally supported).
    std::optional<ProtocolVersion> negotiatedProtocol;

    {
        auto const pvs = parseProtocolVersions(response_["Upgrade"]);

        if (pvs.size() == 1 && isProtocolSupported(pvs[0]))
            negotiatedProtocol = pvs[0];

        if (!negotiatedProtocol)
        {
            fail("processResponse: Unable to negotiate protocol version");
            return;
        }
    }

    auto const sharedValue = makeSharedValue(*streamPtr_, journal_);
    if (!sharedValue)
    {
        shutdown();
        return;  // makeSharedValue logs
    }

    try
    {
        auto const publicKey = verifyHandshake(
            response_,
            *sharedValue,
            overlay_.setup().networkID,
            overlay_.setup().publicIp,
            remoteEndpoint_.address(),
            app_);

        usage_.setPublicKey(publicKey);

        JLOG(journal_.debug()) << "Protocol: " << to_string(*negotiatedProtocol);
        JLOG(journal_.info()) << "Public Key: " << toBase58(TokenType::NodePublic, publicKey);

        auto const member = app_.getCluster().member(publicKey);
        if (member)
        {
            JLOG(journal_.info()) << "Cluster name: " << *member;
        }

        auto const result = overlay_.peerFinder().activate(slot_, publicKey, member.has_value());
        if (result != PeerFinder::Result::Success)
        {
            std::stringstream ss;
            ss << "Outbound Connect Attempt " << remoteEndpoint_ << " " << to_string(result);
            fail(ss.str());
            return;
        }

        if (!socket_.is_open())
            return;

        if (shutdown_)
        {
            tryAsyncShutdown();
            return;
        }

        auto const peer = std::make_shared<PeerImp>(
            app_,
            std::move(streamPtr_),
            readBuf_.data(),
            std::move(slot_),
            std::move(response_),
            usage_,
            publicKey,
            *negotiatedProtocol,
            id_,
            overlay_);

        overlay_.addActive(peer);
    }
    catch (std::exception const& e)
    {
        fail(std::string("Handshake failure (") + e.what() + ")");
        return;
    }
}

}  // namespace xrpl
