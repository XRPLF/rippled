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

#include "ripple.pb.h"
#include <ripple/overlay/impl/OverlayImpl.h>
#include <ripple/overlay/impl/ProtocolMessage.h>
#include <ripple/overlay/impl/TMHello.h>
#include <ripple/overlay/impl/Tuning.h>
#include <ripple/overlay/Message.h>
#include <ripple/protocol/BuildInfo.h>
#include <ripple/protocol/UintTypes.h>
#include <ripple/beast/asio/ssl_bundle.h>
#include <ripple/beast/net/IPAddressConversion.h>
#include <ripple/beast/utility/WrappedSink.h>
#include <beast/core/placeholders.hpp>
#include <beast/core/streambuf.hpp>
#include <beast/http/message.hpp>
#include <beast/http/empty_body.hpp>
#include <beast/http/parser_v1.hpp>
#include <boost/asio/basic_waitable_timer.hpp>
#include <boost/asio/buffers_iterator.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <chrono>
#include <functional>
#include <memory>

namespace ripple {

/** Manages an outbound connection attempt. */
class ConnectAttempt
    : public OverlayImpl::Child
    , public std::enable_shared_from_this<ConnectAttempt>
{
private:
    using error_code = boost::system::error_code;

    using endpoint_type = boost::asio::ip::tcp::endpoint;

    using request_type =
        beast::http::request<beast::http::empty_body>;

    using response_type =
        beast::http::response<beast::http::streambuf_body>;

    Application& app_;
    std::uint32_t const id_;
    beast::WrappedSink sink_;
    beast::Journal journal_;
    endpoint_type remote_endpoint_;
    Resource::Consumer usage_;
    boost::asio::io_service::strand strand_;
    boost::asio::basic_waitable_timer<std::chrono::steady_clock> timer_;
    std::unique_ptr<beast::asio::ssl_bundle> ssl_bundle_;
    beast::asio::ssl_bundle::socket_type& socket_;
    beast::asio::ssl_bundle::stream_type& stream_;
    boost::asio::streambuf read_buf_;
    response_type response_;
    PeerFinder::Slot::ptr slot_;
    request_type req_;

public:
    ConnectAttempt (Application& app, boost::asio::io_service& io_service,
        endpoint_type const& remote_endpoint, Resource::Consumer usage,
            beast::asio::ssl_bundle::shared_context const& context,
                std::uint32_t id, PeerFinder::Slot::ptr const& slot,
                    beast::Journal journal, OverlayImpl& overlay);

    ~ConnectAttempt();

    void
    stop() override;

    void
    run();

private:

    void close();
    void fail (std::string const& reason);
    void fail (std::string const& name, error_code ec);
    void setTimer();
    void cancelTimer();
    void onTimer (error_code ec);
    void onConnect (error_code ec);
    void onHandshake (error_code ec);
    void onWrite (error_code ec);
    void onRead (error_code ec);
    void onShutdown (error_code ec);

    static
    request_type
    makeRequest (bool crawl,
        boost::asio::ip::address const& remote_address);

    void processResponse();

    template <class = void>
    static
    boost::asio::ip::tcp::endpoint
    parse_endpoint (std::string const& s, boost::system::error_code& ec)
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

}

#endif
