#include <xrpl/server/Port.h>

#include <xrpl/basics/contract.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/rfc2616.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/config/Constants.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/impl/network_v4.ipp>
#include <boost/asio/ip/impl/network_v6.ipp>
#include <boost/system/system_error.hpp>

#include <cstdint>
#include <exception>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

namespace xrpl {

bool
Port::secure() const
{
    return protocol.contains("peer") || protocol.contains("https") || protocol.contains("wss") ||
        protocol.contains("wss2");
}

std::string
Port::protocols() const
{
    std::string s;
    for (auto iter = protocol.cbegin(); iter != protocol.cend(); ++iter)
        s += (iter != protocol.cbegin() ? "," : "") + *iter;
    return s;
}

std::ostream&
operator<<(std::ostream& os, Port const& p)
{
    os << "'" << p.name << "' (ip=" << p.ip << ":" << p.port << ", ";

    if (!p.adminNetsV4.empty() || !p.adminNetsV6.empty())
    {
        os << "admin nets:";
        for (auto const& net : p.adminNetsV4)
        {
            os << net.to_string();
            os << ", ";
        }
        for (auto const& net : p.adminNetsV6)
        {
            os << net.to_string();
            os << ", ";
        }
    }

    if (!p.secureGatewayNetsV4.empty() || !p.secureGatewayNetsV6.empty())
    {
        os << "secure_gateway nets:";
        for (auto const& net : p.secureGatewayNetsV4)
        {
            os << net.to_string();
            os << ", ";
        }
        for (auto const& net : p.secureGatewayNetsV6)
        {
            os << net.to_string();
            os << ", ";
        }
    }

    os << p.protocols() << ")";
    return os;
}

//------------------------------------------------------------------------------

static void
populate(
    Section const& section,
    std::string const& field,
    std::ostream& log,
    std::vector<boost::asio::ip::network_v4>& nets4,
    std::vector<boost::asio::ip::network_v6>& nets6)
{
    auto const optResult = section.get(field);
    if (!optResult)
        return;

    std::stringstream ss(*optResult);
    std::string ip;

    while (std::getline(ss, ip, ','))
    {
        boost::algorithm::trim(ip);
        bool v4 = false;
        boost::asio::ip::network_v4 v4Net;
        boost::asio::ip::network_v6 v6Net;

        try
        {
            // First, check to see if 0.0.0.0 or ipv6 equivalent was configured,
            // which means all IP addresses.
            auto const addr = beast::IP::Endpoint::fromStringChecked(ip);
            if (addr)
            {
                if (isUnspecified(*addr))
                {
                    nets4.push_back(boost::asio::ip::make_network_v4("0.0.0.0/0"));
                    nets6.push_back(boost::asio::ip::make_network_v6("::/0"));
                    // No reason to allow more IPs--it would be redundant.
                    break;
                }

                // The configured address is a single IP (or else addr would
                // be unset). We need this to be a subnet, so append
                // the number of network bits to make a subnet of 1,
                // depending on type.
                v4 = addr->isV4();
                std::string addressString = addr->toString();
                if (v4)
                {
                    addressString += "/32";
                    v4Net = boost::asio::ip::make_network_v4(addressString);
                }
                else
                {
                    addressString += "/128";
                    v6Net = boost::asio::ip::make_network_v6(addressString);
                }
            }
            else
            {
                // Since addr is empty, assume that the entry is
                // for a subnet which includes trailing /0-32 or /0-128
                // depending on ip type.
                // First, see if it's an ipv4 subnet. If not, try ipv6.
                // If that throws, then there's nothing we can do with
                // the entry.
                try
                {
                    v4Net = boost::asio::ip::make_network_v4(ip);
                    v4 = true;
                }
                catch (boost::system::system_error const&)
                {
                    v6Net = boost::asio::ip::make_network_v6(ip);
                    v4 = false;
                }
            }

            // Confirm that the address entry is the same as the subnet's
            // underlying network address.
            // 10.1.2.3/24 makes no sense. The underlying network address
            // is 10.1.2.0/24.
            if (v4)
            {
                if (v4Net != v4Net.canonical())
                {
                    log << "The configured subnet " << v4Net.to_string()
                        << " is not the same as the network address, which is "
                        << v4Net.canonical().to_string();
                    Throw<std::exception>();
                }
                nets4.push_back(v4Net);
            }
            else
            {
                if (v6Net != v6Net.canonical())
                {
                    log << "The configured subnet " << v6Net.to_string()
                        << " is not the same as the network address, which is "
                        << v6Net.canonical().to_string();
                    Throw<std::exception>();
                }
                nets6.push_back(v6Net);
            }
        }
        catch (boost::system::system_error const& e)
        {
            log << "Invalid value '" << ip << "' for key '" << field << "' in [" << section.name()
                << "]: " << e.what();
            Throw<std::exception>();
        }
    }
}

void
parsePort(ParsedPort& port, Section const& section, std::ostream& log)
{
    port.name = section.name();
    {
        auto const optResult = section.get(Keys::kIp);
        if (optResult)
        {
            try
            {
                port.ip = boost::asio::ip::make_address(*optResult);
            }
            catch (std::exception const&)
            {
                log << "Invalid value '" << *optResult << "' for key 'ip' in [" << section.name()
                    << "]";
                rethrow();
            }
        }
    }

    {
        auto const optResult = section.get(Keys::kPort);
        if (optResult)
        {
            try
            {
                port.port = beast::lexicalCastThrow<std::uint16_t>(*optResult);

                // Port 0 is not supported for [server]
                if ((*port.port == 0) && (port.name == "server"))
                    Throw<std::exception>();
            }
            catch (std::exception const&)
            {
                log << "Invalid value '" << *optResult << "' for key "
                    << "'port' in [" << section.name() << "]";
                rethrow();
            }
        }
    }

    {
        auto const optResult = section.get(Keys::kProtocol);
        if (optResult)
        {
            for (auto const& s : beast::rfc2616::splitCommas(optResult->begin(), optResult->end()))
                port.protocol.insert(s);
        }
    }

    {
        auto const lim = get(section, Keys::kLimit, "unlimited");

        if (!boost::iequals(lim, "unlimited"))
        {
            try
            {
                port.limit = safeCast<int>(beast::lexicalCastThrow<std::uint16_t>(lim));
            }
            catch (std::exception const&)
            {
                log << "Invalid value '" << lim << "' for key "
                    << "'limit' in [" << section.name() << "]";
                rethrow();
            }
        }
    }

    {
        auto const optResult = section.get(Keys::kSendQueueLimit);
        if (optResult)
        {
            try
            {
                port.wsQueueLimit = beast::lexicalCastThrow<std::uint16_t>(*optResult);

                // Queue must be greater than 0
                if (port.wsQueueLimit == 0)
                    Throw<std::exception>();
            }
            catch (std::exception const&)
            {
                log << "Invalid value '" << *optResult << "' for key "
                    << "'send_queue_limit' in [" << section.name() << "]";
                rethrow();
            }
        }
        else
        {
            // Default Websocket send queue size limit
            port.wsQueueLimit = 100;
        }
    }

    populate(section, Keys::kAdmin, log, port.adminNetsV4, port.adminNetsV6);
    populate(
        section, Keys::kSecureGateway, log, port.secureGatewayNetsV4, port.secureGatewayNetsV6);

    set(port.user, Keys::kUser, section);
    set(port.password, Keys::kPassword, section);
    set(port.adminUser, Keys::kAdminUser, section);
    set(port.adminPassword, Keys::kAdminPassword, section);
    set(port.sslKey, Keys::kSslKey, section);
    set(port.sslCert, Keys::kSslCert, section);
    set(port.sslChain, Keys::kSslChain, section);
    set(port.sslCiphers, Keys::kSslCiphers, section);

    port.pmdOptions.server_enable = section.valueOr(Keys::kPermessageDeflate, true);
    port.pmdOptions.client_max_window_bits = section.valueOr(Keys::kClientMaxWindowBits, 15);
    port.pmdOptions.server_max_window_bits = section.valueOr(Keys::kServerMaxWindowBits, 15);
    port.pmdOptions.client_no_context_takeover =
        section.valueOr(Keys::kClientNoContextTakeover, false);
    port.pmdOptions.server_no_context_takeover =
        section.valueOr(Keys::kServerNoContextTakeover, false);
    port.pmdOptions.compLevel = section.valueOr(Keys::kCompressLevel, 8);
    port.pmdOptions.memLevel = section.valueOr(Keys::kMemoryLevel, 4);
}

}  // namespace xrpl
