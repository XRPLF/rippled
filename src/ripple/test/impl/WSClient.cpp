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

#include <BeastConfig.h>
#include <ripple/test/WSClient.h>
#include <ripple/test/jtx.h>
#include <ripple/json/json_reader.h>
#include <ripple/json/to_string.h>
#include <ripple/server/Port.h>
#include <ripple/wsproto/wsproto.h>
#include <condition_variable>

#include <beast/unit_test/suite.h>

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

    class read_frame_op
    {
        struct data
        {
            WSClientImpl& wsc;
            wsproto::frame_header fh;
            boost::asio::streambuf sb;

            data(WSClientImpl& wsc_)
                : wsc(wsc_)
            {
            }

            ~data()
            {
                wsc.on_read_done();
            }
        };

        std::shared_ptr<data> d_;

    public:
        read_frame_op(read_frame_op const&) = default;
        read_frame_op(read_frame_op&&) = default;

        explicit
        read_frame_op(WSClientImpl& wsc)
            : d_(std::make_shared<data>(wsc))
        {
            read_one();
        }

        void read_one()
        {
            // hack
            d_->sb.consume(d_->sb.size());
            d_->wsc.ws_.async_read_fh(
                d_->fh, std::move(*this));
        }

        void operator()(error_code const& ec)
        {
            if(ec)
                return d_->wsc.on_read_frame(
                    ec, d_->fh, 0, d_->sb.data(),
                        std::move(*this));
            d_->wsc.ws_.async_read(d_->fh,
                d_->sb.prepare(d_->fh.len), std::move(*this));
        }

        void operator()(error_code const& ec,
            wsproto::frame_header const& fh,
                std::size_t bytes_transferred)
        {
            if(ec)
                return d_->wsc.on_read_frame(
                    ec, d_->fh, 0, d_->sb.data(),
                        std::move(*this));
            if(d_->fh.mask)
            {
                // TODO: apply key mask to payload
            }
            d_->sb.commit(bytes_transferred);
            return d_->wsc.on_read_frame(
                ec, d_->fh, bytes_transferred, d_->sb.data(),
                    std::move(*this));
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
            using boost::asio::ip::address_v4;
            if(*pp.ip == address_v4{0x00000000})
                *pp.ip = address_v4{0x7f000001};
            return { *pp.ip, *pp.port };
        }
        throw std::runtime_error("Missing WebSocket port");
    }

    template <class ConstBuffers>
    static
    std::string
    buffer_string (ConstBuffers const& b)
    {
        using namespace boost::asio;
        std::string s;
        s.resize(buffer_size(b));
        buffer_copy(buffer(&s[0], s.size()), b);
        return s;
    }

    boost::asio::io_service ios_;
    boost::optional<
        boost::asio::io_service::work> work_;
    std::thread thread_;
    boost::asio::ip::tcp::socket stream_;
    wsproto::basic_socket<boost::asio::ip::tcp::socket&> ws_;

    // synchronize destructor
    bool b0_ = false;
    std::mutex m0_;
    std::condition_variable cv0_;

    // sychronize message queue
    std::mutex m_;
    std::condition_variable cv_;
    std::list<std::shared_ptr<msg>> msgs_;

public:
    WSClientImpl(Config const& cfg, bool v2)
        : work_(ios_)
        , thread_([&]{ ios_.run(); })
        , stream_(ios_)
        , ws_(stream_)
    {
        using namespace boost::asio;
        stream_.connect(getEndpoint(cfg, v2));
        error_code ec;
        ws_.connect(ec);
        if(ec)
            throw ec;
        read_frame_op{*this};
    }

    ~WSClientImpl() override
    {
        stream_.close();
        {
            std::unique_lock<std::mutex> lock(m0_);
            cv0_.wait(lock, [&]{ return b0_; });
        }
        work_ = boost::none;
        thread_.join();

        //stream_.shutdown(boost::asio::ip::tcp::socket::shutdown_both);
        //stream_.close();
    }

    Json::Value
    invoke(std::string const& cmd,
        Json::Value const& params) override
    {
        using namespace boost::asio;

        {
            Json::Value jp;
            if(params)
               jp = params;
            jp["command"] = cmd;
            auto const s = to_string(jp);
            ws_.write(buffer(s));
        }

        std::shared_ptr<msg> m;
        {
            std::unique_lock<std::mutex> lock(m_);
            cv_.wait(lock, [&]{ return ! msgs_.empty(); });
            m = std::move(msgs_.front());
            msgs_.pop_front();
        }

        return m->jv;
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
            m = std::move(msgs_.front());
            msgs_.pop_front();
        }
        return std::move(m->jv);
    }

private:
    template<class ConstBuffers>
    void
    on_read_frame(error_code const& ec,
        wsproto::frame_header const& fh,
            std::size_t bytes_transferred,
                ConstBuffers const& b,
                    read_frame_op&& op)
    {
        if(bytes_transferred == 0)
            return;
        Json::Value jv;
        Json::Reader jr;
        jr.parse(buffer_string(b), jv);
        auto m = std::make_shared<msg>(
            std::move(jv));
        {
            std::lock_guard<std::mutex> lock(m_);
            msgs_.push_back(m);
            cv_.notify_all();
        }
        op.read_one();
    }

    // Called when the read op terminates
    void
    on_read_done()
    {
        std::lock_guard<std::mutex> lock(m_);
        b0_ = true;
        cv0_.notify_all();
    }
};

std::unique_ptr<WSClient>
makeWSClient(Config const& cfg)
{
    return std::make_unique<WSClientImpl>(cfg, false);
}

std::unique_ptr<WSClient>
makeWS2Client(Config const& cfg)
{
    return std::make_unique<WSClientImpl>(cfg, true);
}

} // test
} // ripple
