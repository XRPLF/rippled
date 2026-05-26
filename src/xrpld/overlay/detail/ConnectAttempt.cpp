#include <xrpld/overlay/detail/ConnectAttempt.h>

#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/Cluster.h>
#include <xrpld/overlay/Peer.h>
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
#include <boost/beast/http/impl/read.hpp>
#include <boost/beast/http/impl/write.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/system/system_error.hpp>

#include <chrono>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace xrpl {

ConnectAttempt::ConnectAttempt(
    Application& app,
    boost::asio::io_context& ioContext,
    endpoint_type remoteEndpoint,
    Resource::Consumer usage,
    shared_context const& context,
    Peer::id_t id,
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
}

void
ConnectAttempt::stop()
{
    if (!strand_.running_in_this_thread())
    {
        boost::asio::post(strand_, std::bind(&ConnectAttempt::stop, shared_from_this()));
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
    setTimer();

    stream_.next_layer().async_connect(
        remoteEndpoint_,
        boost::asio::bind_executor(
            strand_,
            std::bind(&ConnectAttempt::onConnect, shared_from_this(), std::placeholders::_1)));
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
            strand_,
            std::bind(&ConnectAttempt::onTimer, shared_from_this(), std::placeholders::_1)));
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

        fail("onConnect", ec);
        return;
    }

    if (!socket_.is_open())
        return;

    // check if connection has really been established
    socket_.local_endpoint(ec);
    if (ec)
    {
        fail("onConnect", ec);
        return;
    }

    setTimer();

    stream_.set_verify_mode(boost::asio::ssl::verify_none);
    stream_.async_handshake(
        boost::asio::ssl::stream_base::client,
        boost::asio::bind_executor(
            strand_,
            std::bind(&ConnectAttempt::onHandshake, shared_from_this(), std::placeholders::_1)));
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

        fail("onHandshake", ec);
        return;
    }

    auto const localEndpoint = socket_.local_endpoint(ec);
    if (ec)
    {
        fail("onHandshake", ec);
        return;
    }

    if (!overlay_.peerFinder().onConnected(
            slot_, beast::IPAddressConversion::fromAsio(localEndpoint)))
    {
        fail("Duplicate connection");
        return;
    }

    auto const sharedValue = makeSharedValue(*streamPtr_, journal_);
    if (!sharedValue)
    {
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
            std::bind(&ConnectAttempt::onWrite, shared_from_this(), std::placeholders::_1)));
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

        fail("onWrite", ec);
        return;
    }

    boost::beast::http::async_read(
        stream_,
        readBuf_,
        response_,
        boost::asio::bind_executor(
            strand_,
            std::bind(&ConnectAttempt::onRead, shared_from_this(), std::placeholders::_1)));
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
                    std::bind(
                        &ConnectAttempt::onShutdown, shared_from_this(), std::placeholders::_1)));
            return;
        }

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

    if (ec != boost::asio::error::eof)
    {
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

    if (!OverlayImpl::isPeerUpgrade(response_))
    {
        JLOG(journal_.info()) << "Unable to upgrade to peer protocol: " << response_.result()
                              << " (" << response_.reason() << ")";
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
            fail("processResponse: Unable to negotiate protocol version");
            return;
        }
    }

    auto const sharedValue = makeSharedValue(*streamPtr_, journal_);
    if (!sharedValue)
    {
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
        if (result != PeerFinder::Result::Success)
        {
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
    }
    catch (std::exception const& e)
    {
        fail(std::string("Handshake failure (") + e.what() + ")");
        return;
    }
}

}  // namespace xrpl
