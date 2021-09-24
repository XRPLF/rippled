//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

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

#include <ripple/app/sidechain/impl/WebsocketClient.h>

#include <ripple/basics/Log.h>
#include <ripple/json/Output.h>
#include <ripple/json/json_reader.h>
#include <ripple/json/to_string.h>
#include <ripple/protocol/jss.h>
#include <ripple/server/Port.h>

#include <boost/beast/websocket.hpp>

#include <condition_variable>
#include <string>
#include <unordered_map>

#include <iostream>

namespace ripple {
namespace sidechain {

template <class ConstBuffers>
std::string
WebsocketClient::buffer_string(ConstBuffers const& b)
{
    using boost::asio::buffer;
    using boost::asio::buffer_size;
    std::string s;
    s.resize(buffer_size(b));
    buffer_copy(buffer(&s[0], s.size()), b);
    return s;
}

void
WebsocketClient::cleanup()
{
    ios_.post(strand_.wrap([this] {
        if (!peerClosed_)
        {
            {
                std::lock_guard l{m_};
                ws_.async_close({}, strand_.wrap([&](error_code ec) {
                    stream_.cancel(ec);

                    std::lock_guard l(shutdownM_);
                    isShutdown_ = true;
                    shutdownCv_.notify_one();
                }));
            }
        }
        else
        {
            std::lock_guard<std::mutex> l(shutdownM_);
            isShutdown_ = true;
            shutdownCv_.notify_one();
        }
    }));
}

void
WebsocketClient::shutdown()
{
    cleanup();
    std::unique_lock l{shutdownM_};
    shutdownCv_.wait(l, [this] { return isShutdown_; });
}

WebsocketClient::WebsocketClient(
    std::function<void(Json::Value const&)> callback,
    boost::asio::io_service& ios,
    boost::asio::ip::address const& ip,
    std::uint16_t port,
    std::unordered_map<std::string, std::string> const& headers,
    beast::Journal j)
    : ios_(ios)
    , strand_(ios_)
    , stream_(ios_)
    , ws_(stream_)
    , callback_(callback)
    , j_{j}
{
    try
    {
        boost::asio::ip::tcp::endpoint const ep{ip, port};
        stream_.connect(ep);
        ws_.set_option(boost::beast::websocket::stream_base::decorator(
            [&](boost::beast::websocket::request_type& req) {
                for (auto const& h : headers)
                    req.set(h.first, h.second);
            }));
        ws_.handshake(
            ep.address().to_string() + ":" + std::to_string(ep.port()), "/");
        ws_.async_read(
            rb_,
            strand_.wrap(std::bind(
                &WebsocketClient::onReadMsg, this, std::placeholders::_1)));
    }
    catch (std::exception&)
    {
        cleanup();
        Rethrow();
    }
}

WebsocketClient::~WebsocketClient()
{
    cleanup();
}

std::uint32_t
WebsocketClient::send(std::string const& cmd, Json::Value params)
{
    params[jss::method] = cmd;
    params[jss::jsonrpc] = "2.0";
    params[jss::ripplerpc] = "2.0";

    auto const id = nextId_++;
    params[jss::id] = id;
    auto const s = to_string(params);

    std::lock_guard l{m_};
    ws_.write_some(true, boost::asio::buffer(s));
    return id;
}

void
WebsocketClient::onReadMsg(error_code const& ec)
{
    if (ec)
    {
        JLOGV(j_.trace(), "WebsocketClient::onReadMsg error", jv("ec", ec));
        if (ec == boost::beast::websocket::error::closed)
            peerClosed_ = true;
        return;
    }

    Json::Value jv;
    Json::Reader jr;
    jr.parse(buffer_string(rb_.data()), jv);
    rb_.consume(rb_.size());
    callback_(jv);

    std::lock_guard l{m_};
    ws_.async_read(
        rb_,
        strand_.wrap(std::bind(
            &WebsocketClient::onReadMsg, this, std::placeholders::_1)));
}

// Called when the read op terminates
void
WebsocketClient::onReadDone()
{
}

}  // namespace sidechain
}  // namespace ripple
