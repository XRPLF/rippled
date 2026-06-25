#pragma once

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
#include <vector>

namespace boost::asio::ssl {
class context;  // NOLINT(readability-identifier-naming) -- external library name
}  // namespace boost::asio::ssl

namespace xrpl {

class Section;

/** Configuration information for a Server listening port. */
struct Port
{
    explicit Port() = default;

    std::string name;
    boost::asio::ip::address ip;
    std::uint16_t port = 0;
    std::set<std::string, boost::beast::iless> protocol;
    std::vector<boost::asio::ip::network_v4> adminNetsV4;
    std::vector<boost::asio::ip::network_v6> adminNetsV6;
    std::vector<boost::asio::ip::network_v4> secureGatewayNetsV4;
    std::vector<boost::asio::ip::network_v6> secureGatewayNetsV6;
    std::string user;
    std::string password;
    std::string adminUser;
    std::string adminPassword;
    std::string sslKey;
    std::string sslCert;
    std::string sslChain;
    std::string sslCiphers;
    boost::beast::websocket::permessage_deflate pmdOptions;
    std::shared_ptr<boost::asio::ssl::context> context;

    // How many incoming connections are allowed on this
    // port in the range [0, 65535] where 0 means unlimited.
    int limit = 0;

    // Websocket disconnects if send queue exceeds this limit
    std::uint16_t wsQueueLimit{};

    // Returns `true` if any websocket protocols are specified
    [[nodiscard]] bool
    websockets() const;

    // Returns `true` if any secure protocols are specified
    [[nodiscard]] bool
    secure() const;

    // Returns a string containing the list of protocols
    [[nodiscard]] std::string
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
    std::string adminUser;
    std::string adminPassword;
    std::string sslKey;
    std::string sslCert;
    std::string sslChain;
    std::string sslCiphers;
    boost::beast::websocket::permessage_deflate pmdOptions;
    int limit = 0;
    std::uint16_t wsQueueLimit{};

    std::optional<boost::asio::ip::address> ip;
    std::optional<std::uint16_t> port;
    std::vector<boost::asio::ip::network_v4> adminNetsV4;
    std::vector<boost::asio::ip::network_v6> adminNetsV6;
    std::vector<boost::asio::ip::network_v4> secureGatewayNetsV4;
    std::vector<boost::asio::ip::network_v6> secureGatewayNetsV6;
};

void
parsePort(ParsedPort& port, Section const& section, std::ostream& log);

}  // namespace xrpl
