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

#include <test/jtx/WSClient.h>
#include <test/jtx.h>
#include <ripple/json/json_reader.h>
#include <ripple/json/to_string.h>
#include <ripple/protocol/jss.h>
#include <ripple/server/Port.h>
#include <boost/beast/core/multi_buffer.hpp>
#include <boost/beast/websocket.hpp>

#include <condition_variable>
#include <string>
#include <unordered_map>

#include <iostream>

#include <ripple/beast/unit_test.h>

namespace ripple {
namespace test {

class WSClientImpl : public WSClient
{
    using error_code = boost::system::error_code;

    struct msg
    {
        Json::Value jv;

        explicit
        msg(Json::Value&& jv_)
            : jv(jv_)
        {
        }
    };

    static
    boost::asio::ip::tcp::endpoint
    getEndpoint(BasicConfig const& cfg, bool v2)
    {
        auto& log = std::cerr;
        ParsedPort common;
        parse_Port (common, cfg["server"], log);
        auto const ps = v2 ? "ws2" : "ws";
        for (auto const& name : cfg.section("server").values())
        {
            if (! cfg.exists(name))
                continue;
            ParsedPort pp;
            parse_Port(pp, cfg[name], log);
            if(pp.protocol.count(ps) == 0)
                continue;
            using namespace boost::asio::ip;
            if(pp.ip && pp.ip->is_unspecified())
               *pp.ip = pp.ip->is_v6() ? address{address_v6::loopback()} : address{address_v4::loopback()};
            return { *pp.ip, *pp.port };
        }
        Throw<std::runtime_error>("Missing WebSocket port");
        return {}; // Silence compiler control paths return value warning
    }

    template <class ConstBuffers>
    static
    std::string
    buffer_string (ConstBuffers const& b)
    {
        using boost::asio::buffer;
        using boost::asio::buffer_size;
        std::string s;
        s.resize(buffer_size(b));
        buffer_copy(buffer(&s[0], s.size()), b);
        return s;
    }

    boost::asio::io_service ios_;
    boost::optional<
        boost::asio::io_service::work> work_;
    boost::asio::io_service::strand strand_;
    std::thread thread_;
    boost::asio::ip::tcp::socket stream_;
    boost::beast::websocket::stream<boost::asio::ip::tcp::socket&> ws_;
    boost::beast::multi_buffer rb_;

    bool peerClosed_ = false;

    // synchronize destructor
    bool b0_ = false;
    std::mutex m0_;
    std::condition_variable cv0_;

    // synchronize message queue
    std::mutex m_;
    std::condition_variable cv_;
    std::list<std::shared_ptr<msg>> msgs_;

    unsigned rpc_version_;

    void
    cleanup()
    {
        ios_.post(strand_.wrap([this] {
            if (!peerClosed_)
            {
                ws_.async_close({}, strand_.wrap([&](error_code ec) {
                    stream_.cancel(ec);
                }));
            }
        }));
        work_ = boost::none;
        thread_.join();
    }

public:
    WSClientImpl(Config const& cfg, bool v2, unsigned rpc_version,
        std::unordered_map<std::string, std::string> const& headers = {})
        : work_(ios_)
        , strand_(ios_)
        , thread_([&]{ ios_.run(); })
        , stream_(ios_)
        , ws_(stream_)
        , rpc_version_(rpc_version)
    {
        try
        {
            auto const ep = getEndpoint(cfg, v2);
            stream_.connect(ep);
            ws_.handshake_ex(ep.address().to_string() +
                    ":" + std::to_string(ep.port()), "/",
                [&](boost::beast::websocket::request_type &req)
                {
                    for (auto const& h : headers)
                        req.set(h.first, h.second);
                });
            ws_.async_read(rb_,
                strand_.wrap(std::bind(&WSClientImpl::on_read_msg,
                    this, std::placeholders::_1)));
        }
        catch(std::exception&)
        {
            cleanup();
            Rethrow();
        }
    }

    ~WSClientImpl() override
    {
        cleanup();
    }

    Json::Value
    invoke(std::string const& cmd,
        Json::Value const& params) override
    {
        using boost::asio::buffer;
        using namespace std::chrono_literals;

        {
            Json::Value jp;
            if(params)
               jp = params;
            if (rpc_version_ == 2)
            {
                jp[jss::method] = cmd;
                jp[jss::jsonrpc] = "2.0";
                jp[jss::ripplerpc] = "2.0";
                jp[jss::id] = 5;
            }
            else
                jp[jss::command] = cmd;
            auto const s = to_string(jp);
            ws_.write_some(true, buffer(s));
        }

        auto jv = findMsg(5s,
            [&](Json::Value const& jval)
            {
                return jval[jss::type] == jss::response;
            });
        if (jv)
        {
            // Normalize JSON output
            jv->removeMember(jss::type);
            if ((*jv).isMember(jss::status) &&
                (*jv)[jss::status] == jss::error)
            {
                Json::Value ret;
                ret[jss::result] = *jv;
                if ((*jv).isMember(jss::error))
                    ret[jss::error] = (*jv)[jss::error];
                ret[jss::status] = jss::error;
                return ret;
            }
            if ((*jv).isMember(jss::status) &&
                (*jv).isMember(jss::result))
                    (*jv)[jss::result][jss::status] =
                        (*jv)[jss::status];
            return *jv;
        }
        return {};
    }

    boost::optional<Json::Value>
    getMsg(std::chrono::milliseconds const& timeout) override
    {
        std::shared_ptr<msg> m;
        {
            std::unique_lock<std::mutex> lock(m_);
            if(! cv_.wait_for(lock, timeout,
                    [&]{ return ! msgs_.empty(); }))
                return boost::none;
            m = std::move(msgs_.back());
            msgs_.pop_back();
        }
        return std::move(m->jv);
    }

    boost::optional<Json::Value>
    findMsg(std::chrono::milliseconds const& timeout,
        std::function<bool(Json::Value const&)> pred) override
    {
        std::shared_ptr<msg> m;
        {
            std::unique_lock<std::mutex> lock(m_);
            if(! cv_.wait_for(lock, timeout,
                [&]
                {
                    for (auto it = msgs_.begin();
                        it != msgs_.end(); ++it)
                    {
                        if (pred((*it)->jv))
                        {
                            m = std::move(*it);
                            msgs_.erase(it);
                            return true;
                        }
                    }
                    return false;
                }))
            {
                return boost::none;
            }
        }
        return std::move(m->jv);
    }

    unsigned version() const override
    {
        return rpc_version_;
    }

private:
    void
    on_read_msg(error_code const& ec)
    {
        if(ec)
        {
            if(ec == boost::beast::websocket::error::closed)
               peerClosed_ = true;
            return;
        }

        Json::Value jv;
        Json::Reader jr;
        jr.parse(buffer_string(rb_.data()), jv);
        rb_.consume(rb_.size());
        auto m = std::make_shared<msg>(
            std::move(jv));
        {
            std::lock_guard lock(m_);
            msgs_.push_front(m);
            cv_.notify_all();
        }
        ws_.async_read(rb_, strand_.wrap(
            std::bind(&WSClientImpl::on_read_msg,
                this, std::placeholders::_1)));
    }

    // Called when the read op terminates
    void
    on_read_done()
    {
        std::lock_guard lock(m0_);
        b0_ = true;
        cv0_.notify_all();
    }
};

std::unique_ptr<WSClient>
makeWSClient(Config const& cfg, bool v2, unsigned rpc_version,
    std::unordered_map<std::string, std::string> const& headers)
{
    return std::make_unique<WSClientImpl>(cfg, v2, rpc_version, headers);
}

} // test
} // ripple
