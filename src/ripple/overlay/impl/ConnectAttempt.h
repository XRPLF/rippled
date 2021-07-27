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

#ifndef RIPPLE_OVERLAY_CONNECTATTEMPT_H_INCLUDED
#define RIPPLE_OVERLAY_CONNECTATTEMPT_H_INCLUDED

#include <ripple/json/json_reader.h>
#include <ripple/overlay/impl/P2POverlayImpl.h>
#include <ripple/overlay/impl/Tuning.h>

namespace ripple {

/** Manages an outbound connection attempt. */
template <typename PeerImp_t>
class ConnectAttempt
    : public P2POverlayImpl<PeerImp_t>::Child,
      public std::enable_shared_from_this<ConnectAttempt<PeerImp_t>>
{
protected:
    using error_code = boost::system::error_code;

    using endpoint_type = boost::asio::ip::tcp::endpoint;

    using request_type =
        boost::beast::http::request<boost::beast::http::empty_body>;

    using response_type =
        boost::beast::http::response<boost::beast::http::dynamic_body>;

    using socket_type = boost::asio::ip::tcp::socket;
    using middle_type = boost::beast::tcp_stream;
    using stream_type = boost::beast::ssl_stream<middle_type>;
    using shared_context = std::shared_ptr<boost::asio::ssl::context>;

    P2PConfig const& p2pConfig_;
    std::uint32_t const id_;
    beast::WrappedSink sink_;
    beast::Journal const journal_;
    endpoint_type remote_endpoint_;
    Resource::Consumer usage_;
    boost::asio::io_service::strand strand_;
    boost::asio::basic_waitable_timer<std::chrono::steady_clock> timer_;
    std::unique_ptr<stream_type> stream_ptr_;
    socket_type& socket_;
    stream_type& stream_;
    boost::beast::multi_buffer read_buf_;
    response_type response_;
    std::shared_ptr<PeerFinder::Slot> slot_;
    request_type req_;

public:
    ConnectAttempt(
        P2PConfig const& p2pConfig,
        boost::asio::io_service& io_service,
        endpoint_type const& remote_endpoint,
        Resource::Consumer usage,
        shared_context const& context,
        std::uint32_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        beast::Journal journal,
        P2POverlayImpl<PeerImp_t>& overlay);

    ~ConnectAttempt();

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

    template <class = void>
    static boost::asio::ip::tcp::endpoint
    parse_endpoint(std::string const& s, boost::system::error_code& ec)
    {
        beast::IP::Endpoint bep;
        std::istringstream is(s);
        is >> bep;
        if (is.fail())
        {
            ec = boost::system::errc::make_error_code(
                boost::system::errc::invalid_argument);
            return boost::asio::ip::tcp::endpoint{};
        }

        return beast::IPAddressConversion::to_asio_endpoint(bep);
    }
};

template <typename PeerImp_t>
ConnectAttempt<PeerImp_t>::ConnectAttempt(
    P2PConfig const& p2pConfig,
    boost::asio::io_service& io_service,
    endpoint_type const& remote_endpoint,
    Resource::Consumer usage,
    shared_context const& context,
    std::uint32_t id,
    std::shared_ptr<PeerFinder::Slot> const& slot,
    beast::Journal journal,
    P2POverlayImpl<PeerImp_t>& overlay)
    : P2POverlayImpl<PeerImp_t>::Child(overlay)
    , p2pConfig_(p2pConfig)
    , id_(id)
    , sink_(journal, P2POverlayImpl<PeerImp_t>::makePrefix(id))
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

template <typename PeerImp_t>
ConnectAttempt<PeerImp_t>::~ConnectAttempt()
{
    if (slot_ != nullptr)
        this->overlay_.peerFinder().on_closed(slot_);
    JLOG(journal_.trace()) << "~ConnectAttempt";
}

template <typename PeerImp_t>
void
ConnectAttempt<PeerImp_t>::stop()
{
    if (!strand_.running_in_this_thread())
        return strand_.post(
            std::bind(&ConnectAttempt::stop, this->shared_from_this()));
    if (socket_.is_open())
    {
        JLOG(journal_.debug()) << "Stop";
    }
    close();
}

template <typename PeerImp_t>
void
ConnectAttempt<PeerImp_t>::run()
{
    stream_.next_layer().async_connect(
        remote_endpoint_,
        strand_.wrap(std::bind(
            &ConnectAttempt::onConnect,
            this->shared_from_this(),
            std::placeholders::_1)));
}

//------------------------------------------------------------------------------

template <typename PeerImp_t>
void
ConnectAttempt<PeerImp_t>::close()
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

template <typename PeerImp_t>
void
ConnectAttempt<PeerImp_t>::fail(std::string const& reason)
{
    JLOG(journal_.debug()) << reason;
    close();
}

template <typename PeerImp_t>
void
ConnectAttempt<PeerImp_t>::fail(std::string const& name, error_code ec)
{
    JLOG(journal_.debug()) << name << ": " << ec.message();
    close();
}

template <typename PeerImp_t>
void
ConnectAttempt<PeerImp_t>::setTimer()
{
    error_code ec;
    timer_.expires_from_now(std::chrono::seconds(15), ec);
    if (ec)
    {
        JLOG(journal_.error()) << "setTimer: " << ec.message();
        return;
    }

    timer_.async_wait(strand_.wrap(std::bind(
        &ConnectAttempt::onTimer,
        this->shared_from_this(),
        std::placeholders::_1)));
}

template <typename PeerImp_t>
void
ConnectAttempt<PeerImp_t>::cancelTimer()
{
    error_code ec;
    timer_.cancel(ec);
}

template <typename PeerImp_t>
void
ConnectAttempt<PeerImp_t>::onTimer(error_code ec)
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

template <typename PeerImp_t>
void
ConnectAttempt<PeerImp_t>::onConnect(error_code ec)
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
            this->shared_from_this(),
            std::placeholders::_1)));
}

template <typename PeerImp_t>
void
ConnectAttempt<PeerImp_t>::onHandshake(error_code ec)
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

    if (!this->overlay_.peerFinder().onConnected(
            slot_, beast::IPAddressConversion::from_asio(local_endpoint)))
        return fail("Duplicate connection");

    auto const sharedValue = makeSharedValue(*stream_ptr_, journal_);
    if (!sharedValue)
        return close();  // makeSharedValue logs

    req_ = makeRequest(
        !this->overlay_.peerFinder().config().peerPrivate,
        p2pConfig_.config().COMPRESSION,
        p2pConfig_.config().VP_REDUCE_RELAY_ENABLE,
        p2pConfig_.config().LEDGER_REPLAY);

    buildHandshake(
        req_,
        *sharedValue,
        this->overlay_.setup().networkID,
        this->overlay_.setup().public_ip,
        remote_endpoint_.address(),
        p2pConfig_);

    setTimer();
    boost::beast::http::async_write(
        stream_,
        req_,
        strand_.wrap(std::bind(
            &ConnectAttempt::onWrite,
            this->shared_from_this(),
            std::placeholders::_1)));
}

template <typename PeerImp_t>
void
ConnectAttempt<PeerImp_t>::onWrite(error_code ec)
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
            this->shared_from_this(),
            std::placeholders::_1)));
}

template <typename PeerImp_t>
void
ConnectAttempt<PeerImp_t>::onRead(error_code ec)
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
            this->shared_from_this(),
            std::placeholders::_1)));
    }
    if (ec)
        return fail("onRead", ec);
    processResponse();
}

template <typename PeerImp_t>
void
ConnectAttempt<PeerImp_t>::onShutdown(error_code ec)
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

template <typename PeerImp_t>
void
ConnectAttempt<PeerImp_t>::processResponse()
{
    if (response_.result() == boost::beast::http::status::service_unavailable)
    {
        Json::Value json;
        Json::Reader r;
        std::string s;
        s.reserve(boost::asio::buffer_size(response_.body().data()));
        for (auto const buffer : response_.body().data())
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
                    this->overlay_.peerFinder().onRedirects(
                        remote_endpoint_, eps);
                }
            }
        }
    }

    if (!P2POverlayImpl<PeerImp_t>::isPeerUpgrade(response_))
    {
        JLOG(journal_.info())
            << "Unable to upgrade to peer protocol: " << response_.result()
            << " (" << response_.reason() << ")";
        return close();
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
        return close();  // makeSharedValue logs

    try
    {
        auto publicKey = verifyHandshake(
            response_,
            *sharedValue,
            this->overlay_.setup().networkID,
            this->overlay_.setup().public_ip,
            remote_endpoint_.address(),
            p2pConfig_);

        JLOG(journal_.info())
            << "Public Key: " << toBase58(TokenType::NodePublic, publicKey);

        JLOG(journal_.debug())
            << "Protocol: " << to_string(*negotiatedProtocol);

        auto const member = p2pConfig_.clusterMember(publicKey);
        if (member)
        {
            JLOG(journal_.info()) << "Cluster name: " << *member;
        }

        auto const result = this->overlay_.peerFinder().activate(
            slot_, publicKey, static_cast<bool>(member));
        if (result != PeerFinder::Result::success)
            return fail("Outbound slots full");

        this->overlay_.addOutboundPeer(
            std::move(stream_ptr_),
            read_buf_,
            std::move(slot_),
            std::move(response_),
            usage_,
            publicKey,
            *negotiatedProtocol,
            id_);
    }
    catch (std::exception const& e)
    {
        return fail(std::string("Handshake failure (") + e.what() + ")");
    }
}

}  // namespace ripple

#endif
