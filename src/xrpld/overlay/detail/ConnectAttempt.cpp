#include <xrpld/overlay/detail/ConnectAttempt.h>

#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/Cluster.h>
#include <xrpld/overlay/Peer.h>
#include <xrpld/overlay/detail/Handshake.h>
#include <xrpld/overlay/detail/OverlayImpl.h>
#include <xrpld/overlay/detail/PeerImp.h>
#include <xrpld/overlay/detail/PeerSpanNames.h>
#include <xrpld/overlay/detail/ProtocolVersion.h>
#include <xrpld/telemetry/MetricMacros.h>
#ifdef XRPL_ENABLE_TELEMETRY
// The metric-name constants are named only as macro arguments, which the
// macros drop when telemetry is compiled out.
#include <xrpld/telemetry/MetricNames.h>
#endif

#include <xrpl/basics/Log.h>
#include <xrpl/beast/net/IPAddressConversion.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_reader.h>
#include <xrpl/json/json_value.h>
#include <xrpl/peerfinder/Config.h>
#include <xrpl/peerfinder/Slot.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/tokens.h>
#include <xrpl/resource/Consumer.h>
#include <xrpl/telemetry/SpanGuard.h>
#include <xrpl/telemetry/SpanNames.h>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/ssl/stream_base.hpp>
#include <boost/asio/ssl/verify_mode.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/http/impl/read.hpp>
#include <boost/beast/http/impl/write.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/system/system_error.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace xrpl {

ConnectAttempt::ConnectAttempt(
    Application& app,
    boost::asio::io_context& ioContext,
    endpoint_type remoteEndpoint,
    resource::Consumer usage,
    shared_context const& context,
    Peer::id_t id,
    std::shared_ptr<peer_finder::Slot> const& slot,
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
    , streamPtr_(
          std::make_unique<stream_type>(
              socket_type(std::forward<boost::asio::io_context&>(ioContext)),
              *context))
    , socket_(streamPtr_->next_layer().socket())
    , stream_(*streamPtr_)
    , slot_(slot)
{
}

ConnectAttempt::~ConnectAttempt()
{
    if (slot_ != nullptr)
        overlay_.peerFinder().onClosed(slot_);
    JLOG(journal_.trace()) << "~ConnectAttempt";

    // Last resort for an attempt torn down without reaching a terminal path
    // (overlay shutdown, or an operation_aborted early return). The span ends
    // here with no `outcome`, which is the honest record: the dial really did
    // take this long and really did not conclude. Attempts that did conclude
    // already ended their span in reportOutcome(), so this is a no-op for them
    // -- ~optional on an empty handle. Nothing here can throw: ~SpanGuard is
    // noexcept and no attribute is written.
    dialSpan_.reset();
}

void
ConnectAttempt::stop()
{
    if (!strand_.running_in_this_thread())
    {
        boost::asio::post(strand_, [self = shared_from_this()] { self->stop(); });
        return;
    }
    if (socket_.is_open())
    {
        JLOG(journal_.debug()) << "Stop";
    }
    close();
}

void
ConnectAttempt::run()
{
    // Start the dial clock before any async operation is initiated. The
    // constructor is too early (it only builds the object; the caller decides
    // when to dial), and after setTimer() would be too late: setTimer() already
    // queues a strand-bound wait that can read dialStart_ via reportOutcome().
    dialStart_ = std::chrono::steady_clock::now();

    // Span the dial beside the clock it shares, and for the same reason: the
    // constructor is too early and setTimer() already queues a wait that can
    // reach reportOutcome(). A fresh trace root -- a dial is the first thing a
    // starting node does, so there is nothing to parent it to, and freshRoot()
    // stops it inheriting whatever unrelated span happens to be active on the
    // thread that decided to dial.
    {
        using namespace telemetry;
        dialSpan_.emplace(
            SpanGuard::freshRoot(TraceCategory::Peer, seg::peer, peer_span::op::dial));
        if (*dialSpan_)
        {
            // Which peer. Deliberately span-only, never a metric label: one
            // series per peer address would be unbounded cardinality.
            dialSpan_->setAttribute(
                peer_span::attr::remoteEndpoint,
                to_string(beast::IPAddressConversion::fromAsio(remoteEndpoint_)).c_str());
        }
    }

    setTimer();

    stream_.next_layer().async_connect(
        remoteEndpoint_,
        boost::asio::bind_executor(
            strand_, [self = shared_from_this()](error_code const& ec) { self->onConnect(ec); }));
}

void
ConnectAttempt::reportOutcome(std::string_view outcome)
{
    if (outcomeReported_)
        return;
    outcomeReported_ = true;

    // Elapsed time is computed inline, not in a named local, so nothing is
    // left unused when the macros compile away. Microseconds are converted to
    // fractional milliseconds: `duration<double, std::milli>` would put a
    // comma in a non-variadic macro argument, which the preprocessor splits.
    XRPL_METRIC_HISTOGRAM_RECORD(
        app_,
        telemetry::metric::overlayDialLatencyMs,
        "Time from starting an outbound peer dial to its terminal outcome, in milliseconds",
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - dialStart_)
                .count() /
            1000.0);

    XRPL_METRIC_COUNTER_INC_LABELED(
        app_,
        telemetry::metric::overlayConnectTotal,
        "Outbound peer connection attempts, by terminal outcome",
        {{telemetry::label::outcome, std::string(outcome)}});

    // End the span with the SAME outcome value the counter just recorded, from
    // the same funnel, so the two can never disagree. The first-call-wins guard
    // above makes this exactly-once at no extra cost.
    if (dialSpan_)
    {
        if (*dialSpan_)
        {
            using namespace telemetry;
            dialSpan_->setAttribute(peer_span::attr::outcome, outcome);
            // Same elapsed time the histogram above records, stamped on the
            // individual attempt so one slow dial is findable in a trace
            // instead of only visible in an aggregate p95.
            dialSpan_->setAttribute(
                peer_span::attr::durationMs,
                static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                              std::chrono::steady_clock::now() - dialStart_)
                                              .count()));
        }
        // Unconditional, so the span never leaks even when it was inactive, and
        // so the destructor's reset() finds an empty handle.
        dialSpan_.reset();
    }
}

//------------------------------------------------------------------------------

void
ConnectAttempt::close()
{
    XRPL_ASSERT(
        strand_.running_in_this_thread(), "xrpl::ConnectAttempt::close : strand in this thread");
    if (!socket_.is_open())
        return;

    try
    {
        timer_.cancel();
        socket_.close();
    }
    catch (boost::system::system_error const&)  // NOLINT(bugprone-empty-catch)
    {
        // ignored
    }

    JLOG(journal_.debug()) << "Closed";
}

void
ConnectAttempt::fail(std::string const& reason)
{
    JLOG(journal_.debug()) << reason;
    close();
}

void
ConnectAttempt::fail(std::string const& name, error_code ec)
{
    JLOG(journal_.debug()) << name << ": " << ec.message();
    close();
}

void
ConnectAttempt::setTimer()
{
    try
    {
        timer_.expires_after(std::chrono::seconds(15));
    }
    catch (boost::system::system_error const& e)
    {
        JLOG(journal_.error()) << "setTimer: " << e.code();
        return;
    }

    timer_.async_wait(
        boost::asio::bind_executor(
            strand_, [self = shared_from_this()](error_code const& ec) { self->onTimer(ec); }));
}

void
ConnectAttempt::cancelTimer()
{
    try
    {
        timer_.cancel();
    }
    catch (boost::system::system_error const&)  // NOLINT(bugprone-empty-catch)
    {
        // ignored
    }
}

void
ConnectAttempt::onTimer(error_code ec)
{
    if (!socket_.is_open())
        return;

    if (ec)
    {
        // do not initiate shutdown, timers are frequently cancelled
        if (ec == boost::asio::error::operation_aborted)
            return;

        // This should never happen
        JLOG(journal_.error()) << "onTimer: " << ec.message();
        close();
        return;
    }
    reportOutcome(telemetry::peer_span::val::timeout);
    fail("Timeout");
}

void
ConnectAttempt::onConnect(error_code ec)
{
    cancelTimer();

    if (ec)
    {
        if (ec == boost::asio::error::operation_aborted)
            return;

        reportOutcome(telemetry::peer_span::val::tcpFail);
        fail("onConnect", ec);
        return;
    }

    if (!socket_.is_open())
        return;

    // check if connection has really been established
    socket_.local_endpoint(ec);
    if (ec)
    {
        reportOutcome(telemetry::peer_span::val::tcpFail);
        fail("onConnect", ec);
        return;
    }

    setTimer();

    stream_.set_verify_mode(boost::asio::ssl::verify_none);
    stream_.async_handshake(
        boost::asio::ssl::stream_base::client,
        boost::asio::bind_executor(
            strand_, [self = shared_from_this()](error_code const& ec) { self->onHandshake(ec); }));
}

void
ConnectAttempt::onHandshake(error_code ec)
{
    cancelTimer();
    if (!socket_.is_open())
        return;

    if (ec)
    {
        if (ec == boost::asio::error::operation_aborted)
            return;

        reportOutcome(telemetry::peer_span::val::tlsFail);
        fail("onHandshake", ec);
        return;
    }

    auto const localEndpoint = socket_.local_endpoint(ec);
    if (ec)
    {
        reportOutcome(telemetry::peer_span::val::tlsFail);
        fail("onHandshake", ec);
        return;
    }

    if (!overlay_.peerFinder().onConnected(
            slot_, beast::IPAddressConversion::fromAsio(localEndpoint)))
    {
        // Not a TLS failure: the handshake succeeded and PeerFinder then
        // recognised the remote address as our own. Logic::onConnected has
        // exactly one false-returning path and it is the self-connect check
        // ("Logic dropping as self connect"), so this branch means we dialled
        // ourselves -- a local misconfiguration, not an unreachable peer.
        reportOutcome(telemetry::peer_span::val::selfConnection);
        fail("Self connection");
        return;
    }

    auto const sharedValue = makeSharedValue(*streamPtr_, journal_);
    if (!sharedValue)
    {
        reportOutcome(telemetry::peer_span::val::tlsFail);
        close();  // makeSharedValue logs
        return;
    }

    req_ = makeRequest(
        !overlay_.peerFinder().config().peerPrivate,
        app_.config().compression,
        app_.config().ledgerReplay,
        app_.config().txReduceRelayEnable,
        app_.config().vpReduceRelayBaseSquelchEnable);

    buildHandshake(
        req_,
        *sharedValue,
        overlay_.setup().networkID,
        overlay_.setup().publicIp,
        remoteEndpoint_.address(),
        app_);

    setTimer();
    boost::beast::http::async_write(
        stream_,
        req_,
        boost::asio::bind_executor(
            strand_,
            [self = shared_from_this()](error_code const& ec, std::size_t) { self->onWrite(ec); }));
}

void
ConnectAttempt::onWrite(error_code ec)
{
    cancelTimer();

    if (!socket_.is_open())
        return;

    if (ec)
    {
        if (ec == boost::asio::error::operation_aborted)
            return;

        reportOutcome(telemetry::peer_span::val::upgradeFail);
        fail("onWrite", ec);
        return;
    }

    boost::beast::http::async_read(
        stream_,
        readBuf_,
        response_,
        boost::asio::bind_executor(
            strand_,
            [self = shared_from_this()](error_code const& ec, std::size_t) { self->onRead(ec); }));
}

void
ConnectAttempt::onRead(error_code ec)
{
    cancelTimer();

    if (!socket_.is_open())
        return;

    if (ec)
    {
        if (ec == boost::asio::error::operation_aborted)
            return;

        if (ec == boost::asio::error::eof)
        {
            JLOG(journal_.debug()) << "EOF";
            setTimer();
            stream_.async_shutdown(
                boost::asio::bind_executor(
                    strand_,
                    [self = shared_from_this()](error_code const& ec) { self->onShutdown(ec); }));
            return;
        }

        reportOutcome(telemetry::peer_span::val::upgradeFail);
        fail("onRead", ec);
        return;
    }

    processResponse();
}

void
ConnectAttempt::onShutdown(error_code ec)
{
    cancelTimer();
    if (!ec)
    {
        close();
        return;
    }

    // A cancelled shutdown is us tearing the attempt down, not the peer failing
    // it. Every other handler here guards this; without the same guard an
    // ordinary overlay stop was counted as upgrade_fail, inflating that outcome
    // on any node that shuts down while dials are in flight.
    if (ec == boost::asio::error::operation_aborted)
    {
        close();
        return;
    }

    if (ec != boost::asio::error::eof)
    {
        reportOutcome(telemetry::peer_span::val::upgradeFail);
        fail("onShutdown", ec);
        return;
    }
    close();
}

//--------------------------------------------------------------------------

void
ConnectAttempt::processResponse()
{
    if (response_.result() == boost::beast::http::status::service_unavailable)
    {
        json::Value json;
        json::Reader r;
        std::string s;
        s.reserve(boost::asio::buffer_size(response_.body().data()));
        for (auto const buffer : response_.body().data())
        {
            s.append(static_cast<char const*>(buffer.data()), boost::asio::buffer_size(buffer));
        }
        auto const success = r.parse(s, json);
        if (success)
        {
            if (json.isObject() && json.isMember("peer-ips"))
            {
                json::Value const& ips = json["peer-ips"];
                if (ips.isArray())
                {
                    std::vector<boost::asio::ip::tcp::endpoint> eps;
                    eps.reserve(ips.size());
                    for (auto const& v : ips)
                    {
                        if (v.isString())
                        {
                            error_code ec;
                            auto const ep = parseEndpoint(v.asString(), ec);
                            if (!ec)
                                eps.push_back(ep);
                        }
                    }
                    overlay_.peerFinder().onRedirects(remoteEndpoint_, eps);
                }
            }
        }
    }

    // The HTTP-503 block above only harvests redirect hints and falls through,
    // so this is the first terminal point for a response we cannot upgrade.
    if (!OverlayImpl::isPeerUpgrade(response_))
    {
        JLOG(journal_.info()) << "Unable to upgrade to peer protocol: " << response_.result()
                              << " (" << response_.reason() << ")";
        reportOutcome(telemetry::peer_span::val::upgradeFail);
        close();
        return;
    }

    // Just because our peer selected a particular protocol version doesn't
    // mean that it's acceptable to us. Check that it is:
    std::optional<ProtocolVersion> negotiatedProtocol;

    {
        auto const pvs = parseProtocolVersions(response_["Upgrade"]);

        if (pvs.size() == 1 && isProtocolSupported(pvs[0]))
            negotiatedProtocol = pvs[0];

        if (!negotiatedProtocol)
        {
            reportOutcome(telemetry::peer_span::val::upgradeFail);
            fail("processResponse: Unable to negotiate protocol version");
            return;
        }
    }

    auto const sharedValue = makeSharedValue(*streamPtr_, journal_);
    if (!sharedValue)
    {
        reportOutcome(telemetry::peer_span::val::upgradeFail);
        close();  // makeSharedValue logs
        return;
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

        JLOG(journal_.info()) << "Public Key: " << toBase58(TokenType::NodePublic, publicKey);

        JLOG(journal_.debug()) << "Protocol: " << to_string(*negotiatedProtocol);

        auto const member = app_.getCluster().member(publicKey);
        if (member)
        {
            JLOG(journal_.info()) << "Cluster name: " << *member;
        }

        auto const result =
            overlay_.peerFinder().activate(slot_, publicKey, static_cast<bool>(member));
        if (result != peer_finder::Result::Success)
        {
            reportOutcome(telemetry::peer_span::val::upgradeFail);
            fail("Outbound " + std::string(to_string(result)));
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

        // Only after addActive succeeds is the dial genuinely complete. If
        // anything above threw, the catch below reports the failure instead.
        reportOutcome(telemetry::peer_span::val::connected);
    }
    catch (std::exception const& e)
    {
        reportOutcome(telemetry::peer_span::val::upgradeFail);
        fail(std::string("Handshake failure (") + e.what() + ")");
        return;
    }
}

}  // namespace xrpl
