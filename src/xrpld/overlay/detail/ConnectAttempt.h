#pragma once

#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/Peer.h>
#include <xrpld/overlay/detail/OverlayImpl.h>

#include <xrpl/beast/net/IPAddressConversion.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/WrappedSink.h>
#include <xrpl/peerfinder/Slot.h>
#include <xrpl/resource/Consumer.h>
#include <xrpl/telemetry/SpanGuard.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace xrpl {

/**
 * Manages an outbound connection attempt.
 */
class ConnectAttempt : public OverlayImpl::Child,
                       public std::enable_shared_from_this<ConnectAttempt>
{
private:
    using error_code = boost::system::error_code;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using request_type = boost::beast::http::request<boost::beast::http::empty_body>;
    using response_type = boost::beast::http::response<boost::beast::http::dynamic_body>;

    using socket_type = boost::asio::ip::tcp::socket;
    using middle_type = boost::beast::tcp_stream;
    using stream_type = boost::beast::ssl_stream<middle_type>;
    using shared_context = std::shared_ptr<boost::asio::ssl::context>;

    Application& app_;
    std::uint32_t const id_;
    beast::WrappedSink sink_;
    beast::Journal const journal_;
    endpoint_type remoteEndpoint_;
    Resource::Consumer usage_;
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    boost::asio::basic_waitable_timer<std::chrono::steady_clock> timer_;
    std::unique_ptr<stream_type> streamPtr_;
    socket_type& socket_;
    stream_type& stream_;
    boost::beast::multi_buffer readBuf_;
    response_type response_;
    std::shared_ptr<PeerFinder::Slot> slot_;
    request_type req_;

    /**
     * When the dial began, set at the top of run() before any async
     * operation is started. Base for the `overlay_dial_latency_ms`
     * measurement.
     */
    std::chrono::steady_clock::time_point dialStart_;

    /**
     * True once this attempt's outcome has been reported, so the first
     * (most specific) terminal path wins and each attempt is counted at
     * most once. Not atomic on purpose -- see reportOutcome().
     */
    bool outcomeReported_{false};

    /**
     * Spans this dial: started in run() beside `dialStart_`, ended by
     * reportOutcome() on whichever terminal path the state machine takes, and
     * by the destructor for an attempt torn down without one.
     *
     * The counters WP-A1 added answer "how many dials failed, and at which
     * stage". This span answers what they cannot: which peer, and how the time
     * was spent inside one slow attempt. Both are needed because a dial that
     * hangs is invisible in a rate.
     *
     * Thread-free (a SpanGuard holds no thread-local scope). All the completion
     * handlers that end it are bound through `bind_executor(strand_, ...)`, so
     * every access after run() is on one strand; run() itself writes it before
     * any async operation is started, so those handlers see it safely.
     */
    std::optional<telemetry::SpanGuard> dialSpan_;

public:
    ConnectAttempt(
        Application& app,
        boost::asio::io_context& ioContext,
        endpoint_type remoteEndpoint,
        Resource::Consumer usage,
        shared_context const& context,
        Peer::id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        beast::Journal journal,
        OverlayImpl& overlay);

    ~ConnectAttempt() override;

    void
    stop() override;

    void
    run();

private:
    void
    close();
    void
    fail(std::string const& reason);
    void
    fail(std::string const& name, error_code ec);
    void
    setTimer();
    void
    cancelTimer();
    void
    onTimer(error_code ec);
    void
    onConnect(error_code ec);
    void
    onHandshake(error_code ec);
    void
    onWrite(error_code ec);
    void
    onRead(error_code ec);
    void
    onShutdown(error_code ec);
    void
    processResponse();

    /**
     * Record how this outbound dial ended, exactly once per attempt.
     *
     * Every terminal path in the dial state machine funnels here, so the
     * emit code and its cached instruments live in one place instead of
     * being repeated per branch. The first call wins: later calls return
     * immediately, which keeps the reported outcome the most specific one
     * (e.g. a "timeout" is not later overwritten by the "tcp_fail" that
     * the cancelled socket operation reports).
     *
     * Emits:
     *   - `overlay_dial_latency_ms` histogram, no labels
     *   - `overlay_connect_total` counter, label `outcome`
     *   - ends the `peer.dial` span with the same `outcome` value plus
     *     `remote_endpoint` and `duration_ms`
     *
     * The span shares this funnel rather than being ended per branch, so the
     * span's outcome and the counter's label can never disagree, and the
     * first-call-wins guard makes the span exactly-once for free.
     *
     * Dial state machine and where each outcome is reported:
     *
     *   run() ---- dialStart_ = now
     *     |
     *     +-- onTimer  ................................. "timeout"
     *     +-- onConnect      (connect / local_endpoint) . "tcp_fail"
     *     +-- onHandshake
     *     |     +-- TLS handshake / shared value ....... "tls_fail"
     *     |     +-- PeerFinder rejects our own address . "self_connection"
     *     +-- onWrite / onRead / onShutdown ............. "upgrade_fail"
     *     +-- processResponse
     *           +-- bad status / protocol / activate ... "upgrade_fail"
     *           +-- PeerImp created + addActive ........ "connected"
     *
     * The slot branch is drawn separately from the TLS one because
     * `Logic::onConnected` fails for exactly one reason -- the remote address is
     * ours -- and that is a local misconfiguration rather than an unreachable
     * peer.
     *
     * @param outcome One of the `peer_span::val` dial-outcome constants:
     *        `connected`, `tcpFail`, `tlsFail`, `selfConnection`, `upgradeFail`,
     *        `timeout`. Taken as a string_view over a compile-time constant, so
     *        no allocation happens on the caller side. The constants are the
     *        single source for both the counter label and the span attribute, so
     *        the two cannot drift apart.
     *
     * @note Per-connection path: one dial per outbound peer, so this is not
     *       a hot loop.
     * @note Thread safety: every completion handler that calls this is bound
     *       through `bind_executor(strand_, ...)`, so all callers of this
     *       method run on the same strand and `outcomeReported_` needs no
     *       atomic or mutex. `dialStart_` is written once in run(), before
     *       any async operation is initiated, so it is safely visible to
     *       those handlers even though run() itself may execute on the
     *       calling thread rather than the strand.
     * @note Known limitation: an attempt torn down by overlay shutdown
     *       mid-dial (stop() -> close(), or the operation_aborted early
     *       returns) is deliberately not counted -- it has no network
     *       outcome to attribute. The span is still ended, by the destructor,
     *       with no `outcome` attribute: a span whose duration is real but
     *       whose outcome is absent is exactly what "torn down mid-dial"
     *       means, and dropping it would instead hide the attempt.
     * @note MetricsRegistry is already started when this runs:
     *       ApplicationImp::setup() calls startTelemetry() before
     *       ApplicationImp::start() calls overlay_->start(). No-op when
     *       telemetry is compiled out or disabled at runtime.
     */
    void
    reportOutcome(std::string_view outcome);

    template <class = void>
    static boost::asio::ip::tcp::endpoint
    parseEndpoint(std::string const& s, boost::system::error_code& ec)
    {
        beast::IP::Endpoint bep;
        std::istringstream is(s);
        is >> bep;
        if (is.fail())
        {
            ec = boost::system::errc::make_error_code(boost::system::errc::invalid_argument);
            return boost::asio::ip::tcp::endpoint{};
        }

        return beast::IPAddressConversion::toAsioEndpoint(bep);
    }
};

}  // namespace xrpl
