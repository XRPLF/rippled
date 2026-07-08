#include <test/jtx/WSClient.h>

#include <xrpld/core/Config.h>

#include <xrpl/basics/contract.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/config/Constants.h>
#include <xrpl/json/json_reader.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/Port.h>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/address_v6.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core/multi_buffer.hpp>
#include <boost/beast/websocket/error.hpp>
#include <boost/beast/websocket/rfc6455.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <boost/beast/websocket/stream_base.hpp>
#include <boost/system/detail/error_code.hpp>
#include <boost/system/system_error.hpp>

#include <chrono>
#include <condition_variable>
#include <exception>
#include <functional>
#include <iostream>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

namespace xrpl::test {

class WSClientImpl : public WSClient
{
    using error_code = boost::system::error_code;

    struct Msg
    {
        json::Value jv;

        explicit Msg(json::Value&& jv) : jv(std::move(jv))
        {
        }
    };

    static boost::asio::ip::tcp::endpoint
    getEndpoint(BasicConfig const& cfg, bool v2)
    {
        auto& log = std::cerr;
        ParsedPort common;
        parsePort(common, cfg[Sections::kServer], log);
        auto const ps = v2 ? "ws2" : "ws";
        for (auto const& name : cfg.section(Sections::kServer).values())
        {
            if (!cfg.exists(name))
                continue;
            ParsedPort pp;
            parsePort(pp, cfg[name], log);
            if (!pp.protocol.contains(ps))
                continue;
            using namespace boost::asio::ip;
            if (pp.ip && pp.ip->is_unspecified())
            {
                *pp.ip = pp.ip->is_v6() ? address{address_v6::loopback()}
                                        : address{address_v4::loopback()};
            }

            if (!pp.port)
                Throw<std::runtime_error>("Use fixConfigPorts with auto ports");

            return {*pp.ip, *pp.port};  // NOLINT(bugprone-unchecked-optional-access)
        }
        Throw<std::runtime_error>("Missing WebSocket port");
        return {};  // Silence compiler control paths return value warning
    }

    template <class ConstBuffers>
    static std::string
    bufferString(ConstBuffers const& b)
    {
        using boost::asio::buffer;
        using boost::asio::buffer_size;
        std::string s;
        s.resize(buffer_size(b));
        buffer_copy(buffer(&s[0], s.size()), b);
        return s;
    }

    boost::asio::io_context ios_;
    std::optional<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> work_;
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
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
    std::list<std::shared_ptr<Msg>> msgs_;

    unsigned rpcVersion_;

    void
    cleanup()
    {
        boost::asio::post(ios_, boost::asio::bind_executor(strand_, [this] {
                              if (!peerClosed_)
                              {
                                  ws_.async_close(
                                      {}, boost::asio::bind_executor(strand_, [&](error_code) {
                                          try
                                          {
                                              stream_.cancel();
                                          }
                                          // NOLINTNEXTLINE(bugprone-empty-catch)
                                          catch (boost::system::system_error const&)
                                          {
                                              // ignored
                                          }
                                      }));
                              }
                          }));
        work_ = std::nullopt;
        thread_.join();
    }

public:
    WSClientImpl(
        Config const& cfg,
        bool v2,
        unsigned rpcVersion,
        std::unordered_map<std::string, std::string> const& headers = {})
        : work_(std::in_place, boost::asio::make_work_guard(ios_))
        , strand_(boost::asio::make_strand(ios_))
        , thread_([&] { ios_.run(); })
        , stream_(ios_)
        , ws_(stream_)
        , rpcVersion_(rpcVersion)
    {
        try
        {
            auto const ep = getEndpoint(cfg, v2);
            stream_.connect(ep);
            ws_.set_option(
                boost::beast::websocket::stream_base::decorator(
                    [&](boost::beast::websocket::request_type& req) {
                        for (auto const& h : headers)
                            req.set(h.first, h.second);
                    }));
            ws_.handshake(ep.address().to_string() + ":" + std::to_string(ep.port()), "/");
            ws_.async_read(
                rb_,
                boost::asio::bind_executor(
                    strand_, std::bind(&WSClientImpl::onReadMsg, this, std::placeholders::_1)));
        }
        catch (std::exception&)
        {
            cleanup();
            rethrow();
        }
    }

    ~WSClientImpl() override
    {
        cleanup();
    }

    json::Value
    invoke(std::string const& cmd, json::Value const& params) override
    {
        using boost::asio::buffer;
        using namespace std::chrono_literals;

        {
            json::Value jp;
            if (params)
                jp = params;
            if (rpcVersion_ == 2)
            {
                jp[jss::method] = cmd;
                jp[jss::jsonrpc] = "2.0";
                jp[jss::ripplerpc] = "2.0";
                jp[jss::id] = 5;
            }
            else
            {
                jp[jss::command] = cmd;
            }
            auto const s = to_string(jp);

            // Use the error_code overload to avoid an unhandled exception
            // when the server closes the WebSocket connection (e.g. after
            // booting a client that exceeded resource thresholds).
            error_code ec;
            ws_.write_some(true, buffer(s), ec);
            if (ec)
                return {};
        }

        auto jv =
            findMsg(5s, [&](json::Value const& jval) { return jval[jss::type] == jss::response; });
        if (jv)
        {
            // Normalize JSON output
            jv->removeMember(jss::type);
            if ((*jv).isMember(jss::status) && (*jv)[jss::status] == jss::error)
            {
                json::Value ret;
                ret[jss::result] = *jv;
                if ((*jv).isMember(jss::error))
                    ret[jss::error] = (*jv)[jss::error];
                ret[jss::status] = jss::error;
                return ret;
            }
            if ((*jv).isMember(jss::status) && (*jv).isMember(jss::result))
                (*jv)[jss::result][jss::status] = (*jv)[jss::status];
            return *jv;
        }
        return {};
    }

    std::optional<json::Value>
    getMsg(std::chrono::milliseconds const& timeout) override
    {
        std::shared_ptr<Msg> m;
        {
            std::unique_lock<std::mutex> lock(m_);
            if (!cv_.wait_for(lock, timeout, [&] { return !msgs_.empty(); }))
                return std::nullopt;
            m = std::move(msgs_.back());
            msgs_.pop_back();
        }
        return std::move(m->jv);
    }

    std::optional<json::Value>
    findMsg(std::chrono::milliseconds const& timeout, std::function<bool(json::Value const&)> pred)
        override
    {
        std::shared_ptr<Msg> m;
        {
            std::unique_lock<std::mutex> lock(m_);
            if (!cv_.wait_for(lock, timeout, [&] {
                    for (auto it = msgs_.begin(); it != msgs_.end(); ++it)
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
                return std::nullopt;
            }
        }
        return std::move(m->jv);
    }

    [[nodiscard]] unsigned
    version() const override
    {
        return rpcVersion_;
    }

private:
    void
    onReadMsg(error_code const& ec)
    {
        if (ec)
        {
            if (ec == boost::beast::websocket::error::closed)
                peerClosed_ = true;
            return;
        }

        json::Value jv;
        json::Reader jr;
        jr.parse(bufferString(rb_.data()), jv);
        rb_.consume(rb_.size());
        auto m = std::make_shared<Msg>(std::move(jv));
        {
            std::scoped_lock const lock(m_);
            msgs_.push_front(m);
            cv_.notify_all();
        }
        ws_.async_read(
            rb_,
            boost::asio::bind_executor(
                strand_, std::bind(&WSClientImpl::onReadMsg, this, std::placeholders::_1)));
    }

    // Called when the read op terminates
    void
    onReadDone()
    {
        std::scoped_lock const lock(m0_);
        b0_ = true;
        cv0_.notify_all();
    }
};

std::unique_ptr<WSClient>
makeWSClient(
    Config const& cfg,
    bool v2,
    unsigned rpcVersion,
    std::unordered_map<std::string, std::string> const& headers)
{
    return std::make_unique<WSClientImpl>(cfg, v2, rpcVersion, headers);
}

}  // namespace xrpl::test
