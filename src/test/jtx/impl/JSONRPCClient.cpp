#include <test/jtx/JSONRPCClient.h>

#include <xrpl/json/json_reader.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/Port.h>

#include <boost/asio.hpp>
#include <boost/beast/http/dynamic_body.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/write.hpp>

#include <string>

namespace ripple {
namespace test {

class JSONRPCClient : public AbstractClient
{
    static boost::asio::ip::tcp::endpoint
    getEndpoint(BasicConfig const& cfg)
    {
        auto& log = std::cerr;
        ParsedPort common;
        parse_Port(common, cfg["server"], log);
        for (auto const& name : cfg.section("server").values())
        {
            if (!cfg.exists(name))
                continue;
            ParsedPort pp;
            parse_Port(pp, cfg[name], log);
            if (pp.protocol.count("http") == 0)
                continue;
            using namespace boost::asio::ip;
            if (pp.ip && pp.ip->is_unspecified())
                *pp.ip = pp.ip->is_v6() ? address{address_v6::loopback()}
                                        : address{address_v4::loopback()};

            if (!pp.port)
                Throw<std::runtime_error>("Use fixConfigPorts with auto ports");

            return {*pp.ip, *pp.port};
        }
        Throw<std::runtime_error>("Missing HTTP port");
        return {};  // Silence compiler control paths return value warning
    }

    template <class ConstBufferSequence>
    static std::string
    buffer_string(ConstBufferSequence const& b)
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
    unsigned rpc_version_;

public:
    explicit JSONRPCClient(Config const& cfg, unsigned rpc_version)
        : ep_(getEndpoint(cfg)), stream_(ios_), rpc_version_(rpc_version)
    {
        stream_.connect(ep_);
    }

    ~JSONRPCClient() override
    {
        // stream_.shutdown(boost::asio::ip::tcp::socket::shutdown_both);
        // stream_.close();
    }

    /*
        Return value is an Object type with up to three keys:
            status
            error
            result
    */
    Json::Value
    invoke(std::string const& cmd, Json::Value const& params) override
    {
        using namespace boost::beast::http;
        using namespace boost::asio;
        using namespace std::string_literals;

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
            Json::Value jr;
            jr[jss::method] = cmd;
            if (rpc_version_ == 2)
            {
                jr[jss::jsonrpc] = "2.0";
                jr[jss::ripplerpc] = "2.0";
                jr[jss::id] = 5;
            }
            if (params)
            {
                Json::Value& ja = jr[jss::params] = Json::arrayValue;
                ja.append(params);
            }
            req.body() = to_string(jr);
        }
        req.prepare_payload();
        write(stream_, req);

        response<dynamic_body> res;
        read(stream_, bin_, res);

        Json::Reader jr;
        Json::Value jv;
        jr.parse(buffer_string(res.body().data()), jv);
        if (jv["result"].isMember("error"))
            jv["error"] = jv["result"]["error"];
        if (jv["result"].isMember("status"))
            jv["status"] = jv["result"]["status"];
        return jv;
    }

    unsigned
    version() const override
    {
        return rpc_version_;
    }
};

std::unique_ptr<AbstractClient>
makeJSONRPCClient(Config const& cfg, unsigned rpc_version)
{
    return std::make_unique<JSONRPCClient>(cfg, rpc_version);
}

}  // namespace test
}  // namespace ripple
