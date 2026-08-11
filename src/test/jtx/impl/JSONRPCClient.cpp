#include <test/jtx/JSONRPCClient.h>

#include <test/jtx/AbstractClient.h>

#include <xrpld/core/Config.h>

#include <xrpl/basics/contract.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/config/Constants.h>
#include <xrpl/json/json_reader.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/Port.h>

#include <boost/asio/buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/address_v6.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/multi_buffer.hpp>
#include <boost/beast/http/dynamic_body.hpp>
#include <boost/beast/http/error.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/beast/http/write.hpp>
#include <boost/system/system_error.hpp>

#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

namespace xrpl::test {

class JSONRPCClient : public AbstractClient
{
    static boost::asio::ip::tcp::endpoint
    getEndpoint(BasicConfig const& cfg)
    {
        auto& log = std::cerr;
        ParsedPort common;
        parsePort(common, cfg[Sections::kServer], log);
        for (auto const& name : cfg.section(Sections::kServer).values())
        {
            if (!cfg.exists(name))
                continue;
            ParsedPort pp;
            parsePort(pp, cfg[name], log);
            if (not pp.protocol.contains("http"))
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
        Throw<std::runtime_error>("Missing HTTP port");
        return {};  // Silence compiler control paths return value warning
    }

    template <class ConstBufferSequence>
    static std::string
    bufferString(ConstBufferSequence const& b)
    {
        using namespace boost::asio;
        std::string s;
        s.resize(buffer_size(b));
        buffer_copy(buffer(&s[0], s.size()), b);
        return s;
    }

    boost::asio::ip::tcp::endpoint ep_;
    boost::asio::io_context ios_;
    boost::asio::ip::tcp::socket stream_;
    boost::beast::multi_buffer bin_;
    boost::beast::multi_buffer bout_;
    unsigned rpcVersion_;

    bool disconnected_ = false;

    // Errors that mean the persistent keep-alive connection was dropped by the
    // server (rather than a genuine protocol failure), so the request can be
    // safely retried on a fresh connection.
    static bool
    droppedConnection(boost::system::error_code const& ec)
    {
        namespace error = boost::asio::error;
        static auto const kDroppedConnectionErrors = std::to_array<boost::system::error_code>({
            boost::beast::http::error::end_of_stream,
            error::eof,
            error::connection_reset,
            error::connection_aborted,
            error::broken_pipe,
            error::not_connected,
        });

        return std::ranges::any_of(
            kDroppedConnectionErrors,
            [&ec](boost::system::error_code const& e) { return ec == e; });
    }

    // Tear down and re-establish the socket to ep_, discarding any buffered
    // bytes left over from the dropped connection.
    void
    reconnect()
    {
        boost::system::error_code ec;
        stream_.close(ec);
        bin_.clear();
        stream_.connect(ep_);
    }

public:
    explicit JSONRPCClient(Config const& cfg, unsigned rpcVersion)
        : ep_(getEndpoint(cfg)), stream_(ios_), rpcVersion_(rpcVersion)
    {
        stream_.connect(ep_);
    }

    // Return value is an Object type with up to three keys:
    //     status
    //     error
    //     result
    json::Value
    invoke(std::string const& cmd, json::Value const& params) override
    {
        using namespace boost::beast::http;
        using namespace boost::asio;
        using namespace std::string_literals;

        // Once disconnect() has released the slot, the client must not be
        // reused (see AbstractClient::disconnect). Refuse rather than let the
        // failed write/read below trip the reconnect path and silently
        // re-consume a connection slot, which would defeat disconnectClient().
        if (disconnected_)
            Throw<std::logic_error>("JSONRPCClient::invoke called after disconnect()");

        request<string_body> req;
        req.method(boost::beast::http::verb::post);
        req.target("/");
        req.version(11);
        req.insert("Content-Type", "application/json; charset=UTF-8");
        {
            std::ostringstream ostr;
            ostr << ep_;
            req.insert("Host", ostr.str());
        }
        {
            json::Value jr;
            jr[jss::method] = cmd;
            if (rpcVersion_ == 2)
            {
                jr[jss::jsonrpc] = "2.0";
                jr[jss::ripplerpc] = "2.0";
                jr[jss::id] = 5;
            }
            if (params)
            {
                json::Value& ja = jr[jss::params] = json::ValueType::Array;
                ja.append(params);
            }
            req.body() = to_string(jr);
        }
        req.prepare_payload();

        // The client keeps a single keep-alive connection for its whole
        // lifetime, but the server drops idle localhost connections after a few
        // seconds (BaseHTTPPeer::kTimeoutSecondsLocal). If a slow gap between
        // requests let the server close the socket, the write/read here fails
        // with end_of_stream; reconnect and retry the request exactly once.
        response<dynamic_body> res;
        auto writeAndRead = [&] {
            write(stream_, req);
            read(stream_, bin_, res);
        };
        try
        {
            writeAndRead();
        }
        catch (boost::system::system_error const& e)
        {
            if (!droppedConnection(e.code()))
                throw;
            reconnect();
            res = {};
            writeAndRead();
        }

        json::Reader jr;
        json::Value jv;
        jr.parse(bufferString(res.body().data()), jv);
        if (jv["result"].isMember("error"))
            jv["error"] = jv["result"]["error"];
        if (jv["result"].isMember("status"))
            jv["status"] = jv["result"]["status"];
        return jv;
    }

    [[nodiscard]] unsigned
    version() const override
    {
        return rpcVersion_;
    }

    void
    disconnect() override
    {
        if (disconnected_)
            return;

        disconnected_ = true;

        boost::system::error_code ec;
        stream_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        stream_.close(ec);
    }
};

std::unique_ptr<AbstractClient>
makeJSONRPCClient(Config const& cfg, unsigned rpcVersion)
{
    return std::make_unique<JSONRPCClient>(cfg, rpcVersion);
}

}  // namespace xrpl::test
