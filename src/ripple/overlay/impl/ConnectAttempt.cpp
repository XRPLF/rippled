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

#include <ripple/json/json_reader.h>
#include <ripple/overlay/Cluster.h>
#include <ripple/overlay/impl/ConnectAttempt.h>
#include <ripple/overlay/impl/PeerImp.h>
#include <ripple/overlay/impl/ProtocolVersion.h>

namespace ripple {

ConnectAttempt::ConnectAttempt(
    Application& app,
    boost::asio::io_service& io_service,
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
    , strand_(io_service)
    , timer_(io_service)
    , stream_ptr_(std::make_unique<stream_type>(
          socket_type(std::forward<boost::asio::io_service&>(io_service)),
          *context))
    , socket_(stream_ptr_->next_layer().socket())
    , stream_(*stream_ptr_)
    , slot_(slot)
{
    JLOG(journal_.debug()) << "Connect " << remote_endpoint;
}

ConnectAttempt::~ConnectAttempt()
{
    if (slot_ != nullptr)
        overlay_.peerFinder().on_closed(slot_);
    JLOG(journal_.trace()) << "~ConnectAttempt";
}

void
ConnectAttempt::stop()
{
    if (!strand_.running_in_this_thread())
        return strand_.post(
            std::bind(&ConnectAttempt::stop, shared_from_this()));
    if (socket_.is_open())
    {
        JLOG(journal_.debug()) << "Stop";
    }
    close();
}

void
ConnectAttempt::run()
{
    stream_.next_layer().async_connect(
        remote_endpoint_,
        strand_.wrap(std::bind(
            &ConnectAttempt::onConnect,
            shared_from_this(),
            std::placeholders::_1)));
}

//------------------------------------------------------------------------------

void
ConnectAttempt::close()
{
    assert(strand_.running_in_this_thread());
    if (socket_.is_open())
    {
        error_code ec;
        timer_.cancel(ec);
        socket_.close(ec);
        JLOG(journal_.debug()) << "Closed";
    }
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
    error_code ec;
    timer_.expires_from_now(std::chrono::seconds(15), ec);
    if (ec)
    {
        JLOG(journal_.error()) << "setTimer: " << ec.message();
        return;
    }

    timer_.async_wait(strand_.wrap(std::bind(
        &ConnectAttempt::onTimer, shared_from_this(), std::placeholders::_1)));
}

void
ConnectAttempt::cancelTimer()
{
    error_code ec;
    timer_.cancel(ec);
}

void
ConnectAttempt::onTimer(error_code ec)
{
    if (!socket_.is_open())
        return;
    if (ec == boost::asio::error::operation_aborted)
        return;
    if (ec)
    {
        // This should never happen
        JLOG(journal_.error()) << "onTimer: " << ec.message();
        return close();
    }
    fail("Timeout");
}

void
ConnectAttempt::onConnect(error_code ec)
{
    cancelTimer();

    if (ec == boost::asio::error::operation_aborted)
        return;
    endpoint_type local_endpoint;
    if (!ec)
        local_endpoint = socket_.local_endpoint(ec);
    if (ec)
        return fail("onConnect", ec);
    if (!socket_.is_open())
        return;
    JLOG(journal_.trace()) << "onConnect";

    setTimer();
    stream_.set_verify_mode(boost::asio::ssl::verify_none);
    stream_.async_handshake(
        boost::asio::ssl::stream_base::client,
        strand_.wrap(std::bind(
            &ConnectAttempt::onHandshake,
            shared_from_this(),
            std::placeholders::_1)));
}

void
ConnectAttempt::onHandshake(error_code ec)
{
    cancelTimer();
    if (!socket_.is_open())
        return;
    if (ec == boost::asio::error::operation_aborted)
        return;
    endpoint_type local_endpoint;
    if (!ec)
        local_endpoint = socket_.local_endpoint(ec);
    if (ec)
        return fail("onHandshake", ec);
    JLOG(journal_.trace()) << "onHandshake";

    if (!overlay_.peerFinder().onConnected(
            slot_, beast::IPAddressConversion::from_asio(local_endpoint)))
        return fail("Duplicate connection");

    auto const sharedValue = makeSharedValue(*stream_ptr_, journal_);
    if (!sharedValue)
        return close();  // makeSharedValue logs

    req_ = makeRequest(
        !overlay_.peerFinder().config().peerPrivate,
        app_.config().COMPRESSION,
        app_.config().VP_REDUCE_RELAY_ENABLE);

    buildHandshake(
        req_,
        *sharedValue,
        overlay_.setup().networkID,
        overlay_.setup().public_ip,
        remote_endpoint_.address(),
        app_);

    setTimer();
    boost::beast::http::async_write(
        stream_,
        req_,
        strand_.wrap(std::bind(
            &ConnectAttempt::onWrite,
            shared_from_this(),
            std::placeholders::_1)));
}

void
ConnectAttempt::onWrite(error_code ec)
{
    cancelTimer();
    if (!socket_.is_open())
        return;
    if (ec == boost::asio::error::operation_aborted)
        return;
    if (ec)
        return fail("onWrite", ec);
    boost::beast::http::async_read(
        stream_,
        read_buf_,
        response_,
        strand_.wrap(std::bind(
            &ConnectAttempt::onRead,
            shared_from_this(),
            std::placeholders::_1)));
}

void
ConnectAttempt::onRead(error_code ec)
{
    cancelTimer();

    if (!socket_.is_open())
        return;
    if (ec == boost::asio::error::operation_aborted)
        return;
    if (ec == boost::asio::error::eof)
    {
        JLOG(journal_.info()) << "EOF";
        setTimer();
        return stream_.async_shutdown(strand_.wrap(std::bind(
            &ConnectAttempt::onShutdown,
            shared_from_this(),
            std::placeholders::_1)));
    }
    if (ec)
        return fail("onRead", ec);
    processResponse();
}

void
ConnectAttempt::onShutdown(error_code ec)
{
    cancelTimer();
    if (!ec)
    {
        JLOG(journal_.error()) << "onShutdown: expected error condition";
        return close();
    }
    if (ec != boost::asio::error::eof)
        return fail("onShutdown", ec);
    close();
}

//--------------------------------------------------------------------------

void
ConnectAttempt::processResponse()
{
    if (response_.result() == boost::beast::http::status::service_unavailable)
    {
        Json::Value json;
        Json::Reader r;
        std::string s;
        s.reserve(boost::asio::buffer_size(response_.body().data()));
        for (auto const& buffer : response_.body().data())
            s.append(
                boost::asio::buffer_cast<char const*>(buffer),
                boost::asio::buffer_size(buffer));
        auto const success = r.parse(s, json);
        if (success)
        {
            if (json.isObject() && json.isMember("peer-ips"))
            {
                Json::Value const& ips = json["peer-ips"];
                if (ips.isArray())
                {
                    std::vector<boost::asio::ip::tcp::endpoint> eps;
                    eps.reserve(ips.size());
                    for (auto const& v : ips)
                    {
                        if (v.isString())
                        {
                            error_code ec;
                            auto const ep = parse_endpoint(v.asString(), ec);
                            if (!ec)
                                eps.push_back(ep);
                        }
                    }
                    overlay_.peerFinder().onRedirects(remote_endpoint_, eps);
                }
            }
        }
    }

    if (!OverlayImpl::isPeerUpgrade(response_))
    {
        JLOG(journal_.info())
            << "Unable to upgrade to peer protocol: " << response_.result()
            << " (" << response_.reason() << ")";
        return close();
    }

    // Just because our peer selected a particular protocol version doesn't
    // mean that it's acceptable to us. Check that it is:
    boost::optional<ProtocolVersion> negotiatedProtocol;

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
        return close();  // makeSharedValue logs

    try
    {
        auto publicKey = verifyHandshake(
            response_,
            *sharedValue,
            overlay_.setup().networkID,
            overlay_.setup().public_ip,
            remote_endpoint_.address(),
            app_);

        JLOG(journal_.info())
            << "Public Key: " << toBase58(TokenType::NodePublic, publicKey);

        JLOG(journal_.debug())
            << "Protocol: " << to_string(*negotiatedProtocol);

        auto const member = app_.cluster().member(publicKey);
        if (member)
        {
            JLOG(journal_.info()) << "Cluster name: " << *member;
        }

        auto const result = overlay_.peerFinder().activate(
            slot_, publicKey, static_cast<bool>(member));
        if (result != PeerFinder::Result::success)
            return fail("Outbound slots full");

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
