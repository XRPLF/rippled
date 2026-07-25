#pragma once

#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/Peer.h>
#include <xrpld/overlay/detail/OverlayImpl.h>
#include <xrpld/peerfinder/Slot.h>

#include <xrpl/beast/net/IPAddressConversion.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/WrappedSink.h>
#include <xrpl/resource/Consumer.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>

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
     *
     * Dial state machine and where each outcome is reported:
     *
     *   run() ---- dialStart_ = now
     *     |
     *     +-- onTimer  ................................. "timeout"
     *     +-- onConnect      (connect / local_endpoint) . "tcp_fail"
     *     +-- onHandshake    (TLS / slot / shared value)  "tls_fail"
     *     +-- onWrite / onRead / onShutdown ............. "upgrade_fail"
     *     +-- processResponse
     *           +-- bad status / protocol / activate ... "upgrade_fail"
     *           +-- PeerImp created + addActive ........ "connected"
     *
     * @param outcome One of "connected", "tcp_fail", "tls_fail",
     *        "upgrade_fail", "timeout". A string literal, so no allocation
     *        happens on the caller side.
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
     *       outcome to attribute.
     * @note MetricsRegistry is already started when this runs:
     *       ApplicationImp::setup() calls startTelemetry() before
     *       ApplicationImp::start() calls overlay_->start(). No-op when
     *       telemetry is compiled out or disabled at runtime.
     */
    void
    reportOutcome(char const* outcome);

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
