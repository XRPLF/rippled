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

#include <ripple/overlay/impl/OverlayImpl.h>
#include <ripple/overlay/impl/Tuning.h>

namespace ripple {

/** Manages an outbound connection attempt. */
class ConnectAttempt : public OverlayImpl::Child,
                       public std::enable_shared_from_this<ConnectAttempt>
{
private:
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

    Application& app_;
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
        Application& app,
        boost::asio::io_service& io_service,
        endpoint_type const& remote_endpoint,
        Resource::Consumer usage,
        shared_context const& context,
        std::uint32_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        beast::Journal journal,
        OverlayImpl& overlay);

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

}  // namespace ripple

#endif
