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

#include <xrpld/overlay/Cluster.h>
#include <xrpld/overlay/detail/ConnectAttempt.h>
#include <xrpld/overlay/detail/PeerImp.h>
#include <xrpld/overlay/detail/ProtocolVersion.h>

#include <xrpl/json/json_reader.h>

#include <sstream>

namespace ripple {

ConnectAttempt::ConnectAttempt(
    Application& app,
    boost::asio::io_context& io_context,
    endpoint_type const& remote_endpoint,
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
    , remote_endpoint_(remote_endpoint)
    , usage_(usage)
    , strand_(boost::asio::make_strand(io_context))
    , timer_(io_context)
    , stepTimer_(io_context)
    , stream_ptr_(std::make_unique<stream_type>(
          socket_type(std::forward<boost::asio::io_context&>(io_context)),
          *context))
    , socket_(stream_ptr_->next_layer().socket())
    , stream_(*stream_ptr_)
    , slot_(slot)
{
}

ConnectAttempt::~ConnectAttempt()
{
    // slot_ will be null if we successfully connected
    // and transferred ownership to a PeerImp
    if (slot_ != nullptr)
        overlay_.peerFinder().on_closed(slot_);
}

void
ConnectAttempt::stop()
{
    if (!strand_.running_in_this_thread())
        return boost::asio::post(
            strand_, std::bind(&ConnectAttempt::stop, shared_from_this()));

    if (!socket_.is_open())
        return;

    JLOG(journal_.debug()) << "stop: Stop";

    shutdown();
}

void
ConnectAttempt::run()
{
    if (!strand_.running_in_this_thread())
        return boost::asio::post(
            strand_, std::bind(&ConnectAttempt::run, shared_from_this()));

    JLOG(journal_.debug()) << "run: connecting to " << remote_endpoint_;

    ioPending_ = true;

    // Allow up to connectTimeout_ seconds to establish remote peer connection
    setTimer(ConnectionStep::TcpConnect);

    stream_.next_layer().async_connect(
        remote_endpoint_,
        boost::asio::bind_executor(
            strand_,
            std::bind(
                &ConnectAttempt::onConnect,
                shared_from_this(),
                std::placeholders::_1)));
}

//------------------------------------------------------------------------------

void
ConnectAttempt::shutdown()
{
    XRPL_ASSERT(
        strand_.running_in_this_thread(),
        "ripple::ConnectAttempt::shutdown: strand in this thread");

    if (!socket_.is_open())
        return;

    shutdown_ = true;
    boost::beast::get_lowest_layer(stream_).cancel();

    tryAsyncShutdown();
}

void
ConnectAttempt::tryAsyncShutdown()
{
    XRPL_ASSERT(
        strand_.running_in_this_thread(),
        "ripple::ConnectAttempt::tryAsyncShutdown : strand in this thread");

    if (!shutdown_ || currentStep_ == ConnectionStep::ShutdownStarted)
        return;

    if (ioPending_)
        return;

    // gracefully shutdown the SSL socket, performing a shutdown handshake
    if (currentStep_ != ConnectionStep::TcpConnect &&
        currentStep_ != ConnectionStep::TlsHandshake)
    {
        setTimer(ConnectionStep::ShutdownStarted);
        return stream_.async_shutdown(bind_executor(
            strand_,
            std::bind(
                &ConnectAttempt::onShutdown,
                shared_from_this(),
                std::placeholders::_1)));
    }

    close();
}

void
ConnectAttempt::onShutdown(error_code ec)
{
    cancelTimer();

    if (ec)
    {
        // - eof: the stream was cleanly closed
        // - operation_aborted: an expired timer (slow shutdown)
        // - stream_truncated: the tcp connection closed (no handshake) it could
        // occur if a peer does not perform a graceful disconnect
        // - broken_pipe: the peer is gone
        // - application data after close notify: benign SSL shutdown condition
        bool shouldLog =
            (ec != boost::asio::error::eof &&
             ec != boost::asio::error::operation_aborted &&
             ec.message().find("application data after close notify") ==
                 std::string::npos);

        if (shouldLog)
        {
            JLOG(journal_.debug()) << "onShutdown: " << ec.message();
        }
    }

    close();
}

void
ConnectAttempt::close()
{
    XRPL_ASSERT(
        strand_.running_in_this_thread(),
        "ripple::ConnectAttempt::close : strand in this thread");
    if (!socket_.is_open())
        return;

    cancelTimer();

    error_code ec;
    socket_.close(ec);
}

void
ConnectAttempt::fail(std::string const& reason)
{
    JLOG(journal_.debug()) << reason;
    shutdown();
}

void
ConnectAttempt::fail(std::string const& name, error_code ec)
{
    JLOG(journal_.debug()) << name << ": " << ec.message();
    shutdown();
}

void
ConnectAttempt::setTimer(ConnectionStep step)
{
    currentStep_ = step;

    // Set global timer (only if not already set)
    if (timer_.expiry() == std::chrono::steady_clock::time_point{})
    {
        try
        {
            timer_.expires_after(connectTimeout);
            timer_.async_wait(boost::asio::bind_executor(
                strand_,
                std::bind(
                    &ConnectAttempt::onTimer,
                    shared_from_this(),
                    std::placeholders::_1)));
        }
        catch (std::exception const& ex)
        {
            JLOG(journal_.error()) << "setTimer (global): " << ex.what();
            return close();
        }
    }

    // Set step-specific timer
    try
    {
        std::chrono::seconds stepTimeout;
        switch (step)
        {
            case ConnectionStep::TcpConnect:
                stepTimeout = StepTimeouts::tcpConnect;
                break;
            case ConnectionStep::TlsHandshake:
                stepTimeout = StepTimeouts::tlsHandshake;
                break;
            case ConnectionStep::HttpWrite:
                stepTimeout = StepTimeouts::httpWrite;
                break;
            case ConnectionStep::HttpRead:
                stepTimeout = StepTimeouts::httpRead;
                break;
            case ConnectionStep::ShutdownStarted:
                stepTimeout = StepTimeouts::tlsShutdown;
                break;
            case ConnectionStep::Complete:
            case ConnectionStep::Init:
                return;  // No timer needed for init or complete step
        }

        // call to expires_after cancels previous timer
        stepTimer_.expires_after(stepTimeout);
        stepTimer_.async_wait(boost::asio::bind_executor(
            strand_,
            std::bind(
                &ConnectAttempt::onTimer,
                shared_from_this(),
                std::placeholders::_1)));

        JLOG(journal_.trace()) << "setTimer: " << stepToString(step)
                               << " timeout=" << stepTimeout.count() << "s";
    }
    catch (std::exception const& ex)
    {
        JLOG(journal_.error())
            << "setTimer (step " << stepToString(step) << "): " << ex.what();
        return close();
    }
}

void
ConnectAttempt::cancelTimer()
{
    try
    {
        timer_.cancel();
        stepTimer_.cancel();
    }
    catch (boost::system::system_error const&)
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
        return close();
    }

    // Determine which timer expired by checking their expiry times
    auto const now = std::chrono::steady_clock::now();
    bool globalExpired = (timer_.expiry() <= now);
    bool stepExpired = (stepTimer_.expiry() <= now);

    if (globalExpired)
    {
        JLOG(journal_.debug())
            << "onTimer: Global timeout; step: " << stepToString(currentStep_);
    }
    else if (stepExpired)
    {
        JLOG(journal_.debug())
            << "onTimer: Step timeout; step: " << stepToString(currentStep_);
    }
    else
    {
        JLOG(journal_.warn()) << "onTimer: Unexpected timer callback";
    }

    close();
}

void
ConnectAttempt::onConnect(error_code ec)
{
    ioPending_ = false;

    if (ec)
    {
        if (ec == boost::asio::error::operation_aborted)
            return tryAsyncShutdown();

        return fail("onConnect", ec);
    }

    if (!socket_.is_open())
        return;

    // check if connection has really been established
    socket_.local_endpoint(ec);
    if (ec)
        return fail("onConnect", ec);

    if (shutdown_)
        return tryAsyncShutdown();

    ioPending_ = true;

    setTimer(ConnectionStep::TlsHandshake);

    stream_.set_verify_mode(boost::asio::ssl::verify_none);
    stream_.async_handshake(
        boost::asio::ssl::stream_base::client,
        boost::asio::bind_executor(
            strand_,
            std::bind(
                &ConnectAttempt::onHandshake,
                shared_from_this(),
                std::placeholders::_1)));
}

void
ConnectAttempt::onHandshake(error_code ec)
{
    ioPending_ = false;

    if (ec)
    {
        if (ec == boost::asio::error::operation_aborted)
            return tryAsyncShutdown();

        return fail("onHandshake", ec);
    }

    auto const local_endpoint = socket_.local_endpoint(ec);
    if (ec)
        return fail("onHandshake", ec);

    setTimer(ConnectionStep::HttpWrite);

    // check if we connected to ourselves
    if (!overlay_.peerFinder().onConnected(
            slot_, beast::IPAddressConversion::from_asio(local_endpoint)))
        return fail("Self connection");

    auto const sharedValue = makeSharedValue(*stream_ptr_, journal_);
    if (!sharedValue)
        return shutdown();  // makeSharedValue logs

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
        overlay_.setup().public_ip,
        remote_endpoint_.address(),
        app_);

    if (shutdown_)
        return tryAsyncShutdown();

    ioPending_ = true;

    boost::beast::http::async_write(
        stream_,
        req_,
        boost::asio::bind_executor(
            strand_,
            std::bind(
                &ConnectAttempt::onWrite,
                shared_from_this(),
                std::placeholders::_1)));
}

void
ConnectAttempt::onWrite(error_code ec)
{
    ioPending_ = false;

    if (ec)
    {
        if (ec == boost::asio::error::operation_aborted)
            return tryAsyncShutdown();

        return fail("onWrite", ec);
    }

    if (shutdown_)
        return tryAsyncShutdown();

    ioPending_ = true;

    setTimer(ConnectionStep::HttpRead);

    boost::beast::http::async_read(
        stream_,
        read_buf_,
        response_,
        boost::asio::bind_executor(
            strand_,
            std::bind(
                &ConnectAttempt::onRead,
                shared_from_this(),
                std::placeholders::_1)));
}

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
            return shutdown();
        }

        if (ec == boost::asio::error::operation_aborted)
            return tryAsyncShutdown();

        return fail("onRead", ec);
    }

    if (shutdown_)
        return tryAsyncShutdown();

    processResponse();
}

//--------------------------------------------------------------------------

void
ConnectAttempt::processResponse()
{
    if (!OverlayImpl::isPeerUpgrade(response_))
    {
        // A peer may respond with service_unavailable and a list of alternative
        // peers to connect to, a differing status code is unexpected
        if (response_.result() !=
            boost::beast::http::status::service_unavailable)
        {
            JLOG(journal_.warn())
                << "Unable to upgrade to peer protocol: " << response_.result()
                << " (" << response_.reason() << ")";
            return shutdown();
        }

        // Parse response body to determine if this is a redirect or other
        // service unavailable
        std::string responseBody;
        responseBody.reserve(boost::asio::buffer_size(response_.body().data()));
        for (auto const buffer : response_.body().data())
            responseBody.append(
                static_cast<char const*>(buffer.data()),
                boost::asio::buffer_size(buffer));

        Json::Value json;
        Json::Reader reader;
        auto const isValidJson = reader.parse(responseBody, json);

        // Check if this is a redirect response (contains peer-ips field)
        auto const isRedirect =
            isValidJson && json.isObject() && json.isMember("peer-ips");

        if (!isRedirect)
        {
            JLOG(journal_.warn())
                << "processResponse: " << remote_endpoint_
                << " failed to upgrade to peer protocol: " << response_.result()
                << " (" << response_.reason() << ")";

            return shutdown();
        }

        Json::Value const& peerIps = json["peer-ips"];
        if (!peerIps.isArray())
            return fail("processResponse: invalid peer-ips format");

        // Extract and validate peer endpoints
        std::vector<boost::asio::ip::tcp::endpoint> redirectEndpoints;
        redirectEndpoints.reserve(peerIps.size());

        for (auto const& ipValue : peerIps)
        {
            if (!ipValue.isString())
                continue;

            error_code ec;
            auto const endpoint = parse_endpoint(ipValue.asString(), ec);
            if (!ec)
                redirectEndpoints.push_back(endpoint);
        }

        // Notify PeerFinder about the redirect redirectEndpoints may be empty
        overlay_.peerFinder().onRedirects(remote_endpoint_, redirectEndpoints);

        return fail("processResponse: failed to connect to peer: redirected");
    }

    // Just because our peer selected a particular protocol version doesn't
    // mean that it's acceptable to us. Check that it is:
    std::optional<ProtocolVersion> negotiatedProtocol;

    {
        auto const pvs = parseProtocolVersions(response_["Upgrade"]);

        if (pvs.size() == 1 && isProtocolSupported(pvs[0]))
            negotiatedProtocol = pvs[0];

        if (!negotiatedProtocol)
            return fail(
                "processResponse: Unable to negotiate protocol version");
    }

    auto const sharedValue = makeSharedValue(*stream_ptr_, journal_);
    if (!sharedValue)
        return shutdown();  // makeSharedValue logs

    try
    {
        auto const publicKey = verifyHandshake(
            response_,
            *sharedValue,
            overlay_.setup().networkID,
            overlay_.setup().public_ip,
            remote_endpoint_.address(),
            app_);

        JLOG(journal_.debug())
            << "Protocol: " << to_string(*negotiatedProtocol);
        JLOG(journal_.info())
            << "Public Key: " << toBase58(TokenType::NodePublic, publicKey);

        auto const member = app_.cluster().member(publicKey);
        if (member)
        {
            JLOG(journal_.info()) << "Cluster name: " << *member;
        }

        auto const result = overlay_.peerFinder().activate(
            slot_, publicKey, member.has_value());
        if (result != PeerFinder::Result::success)
        {
            std::stringstream ss;
            ss << "Outbound Connect Attempt " << remote_endpoint_ << " "
               << to_string(result);
            return fail(ss.str());
        }

        if (!socket_.is_open())
            return;

        if (shutdown_)
            return tryAsyncShutdown();

        auto const peer = std::make_shared<PeerImp>(
            app_,
            std::move(stream_ptr_),
            read_buf_.data(),
            std::move(slot_),
            std::move(response_),
            usage_,
            publicKey,
            *negotiatedProtocol,
            id_,
            overlay_);

        overlay_.add_active(peer);
    }
    catch (std::exception const& e)
    {
        return fail(std::string("Handshake failure (") + e.what() + ")");
    }
}

}  // namespace ripple
