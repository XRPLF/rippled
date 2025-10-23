#ifndef XRPL_SERVER_PORT_H_INCLUDED
#define XRPL_SERVER_PORT_H_INCLUDED

#include <xrpl/basics/BasicConfig.h>
#include <xrpl/beast/net/IPEndpoint.h>

#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/network_v4.hpp>
#include <boost/asio/ip/network_v6.hpp>
#include <boost/beast/core/string.hpp>
#include <boost/beast/websocket/option.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>

namespace boost {
namespace asio {
namespace ssl {
class context;
}
}  // namespace asio
}  // namespace boost

namespace ripple {

/** Configuration information for a Server listening port. */
struct Port
{
    explicit Port() = default;

    std::string name;
    boost::asio::ip::address ip;
    std::uint16_t port = 0;
    std::set<std::string, boost::beast::iless> protocol;
    std::vector<boost::asio::ip::network_v4> admin_nets_v4;
    std::vector<boost::asio::ip::network_v6> admin_nets_v6;
    std::vector<boost::asio::ip::network_v4> secure_gateway_nets_v4;
    std::vector<boost::asio::ip::network_v6> secure_gateway_nets_v6;
    std::string user;
    std::string password;
    std::string admin_user;
    std::string admin_password;
    std::string ssl_key;
    std::string ssl_cert;
    std::string ssl_chain;
    std::string ssl_ciphers;
    boost::beast::websocket::permessage_deflate pmd_options;
    std::shared_ptr<boost::asio::ssl::context> context;

    // How many incoming connections are allowed on this
    // port in the range [0, 65535] where 0 means unlimited.
    int limit = 0;

    // Websocket disconnects if send queue exceeds this limit
    std::uint16_t ws_queue_limit;

    // Returns `true` if any websocket protocols are specified
    bool
    websockets() const;

    // Returns `true` if any secure protocols are specified
    bool
    secure() const;

    // Returns a string containing the list of protocols
    std::string
    protocols() const;
};

std::ostream&
operator<<(std::ostream& os, Port const& p);

//------------------------------------------------------------------------------

struct ParsedPort
{
    explicit ParsedPort() = default;

    std::string name;
    std::set<std::string, boost::beast::iless> protocol;
    std::string user;
    std::string password;
    std::string admin_user;
    std::string admin_password;
    std::string ssl_key;
    std::string ssl_cert;
    std::string ssl_chain;
    std::string ssl_ciphers;
    boost::beast::websocket::permessage_deflate pmd_options;
    int limit = 0;
    std::uint16_t ws_queue_limit;

    std::optional<boost::asio::ip::address> ip;
    std::optional<std::uint16_t> port;
    std::vector<boost::asio::ip::network_v4> admin_nets_v4;
    std::vector<boost::asio::ip::network_v6> admin_nets_v6;
    std::vector<boost::asio::ip::network_v4> secure_gateway_nets_v4;
    std::vector<boost::asio::ip::network_v6> secure_gateway_nets_v6;
};

void
parse_Port(ParsedPort& port, Section const& section, std::ostream& log);

}  // namespace ripple

#endif
