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

#include <ripple/basics/safe_cast.h>
#include <ripple/beast/core/LexicalCast.h>
#include <ripple/beast/rfc2616.h>
#include <ripple/server/Port.h>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <sstream>

namespace ripple {

bool
Port::secure() const
{
    return protocol.count("peer") > 0 || protocol.count("https") > 0 ||
        protocol.count("wss") > 0 || protocol.count("wss2") > 0;
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

    if (p.admin_nets_v4.size() || p.admin_nets_v6.size())
    {
        os << "admin nets:";
        for (auto const& net : p.admin_nets_v4)
        {
            os << net.to_string();
            os << ", ";
        }
        for (auto const& net : p.admin_nets_v6)
        {
            os << net.to_string();
            os << ", ";
        }
    }

    if (p.secure_gateway_nets_v4.size() || p.secure_gateway_nets_v6.size())
    {
        os << "secure_gateway nets:";
        for (auto const& net : p.secure_gateway_nets_v4)
        {
            os << net.to_string();
            os << ", ";
        }
        for (auto const& net : p.secure_gateway_nets_v6)
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
        bool v4;
        boost::asio::ip::network_v4 v4Net;
        boost::asio::ip::network_v6 v6Net;

        try
        {
            // First, check to see if 0.0.0.0 or ipv6 equivalent was configured,
            // which means all IP addresses.
            auto const addr = beast::IP::Endpoint::from_string_checked(ip);
            if (addr)
            {
                if (is_unspecified(*addr))
                {
                    nets4.push_back(
                        boost::asio::ip::make_network_v4("0.0.0.0/0"));
                    nets6.push_back(boost::asio::ip::make_network_v6("::/0"));
                    // No reason to allow more IPs--it would be redundant.
                    break;
                }

                // The configured address is a single IP (or else addr would
                // be unset). We need this to be a subnet, so append
                // the number of network bits to make a subnet of 1,
                // depending on type.
                v4 = addr->is_v4();
                std::string addressString = addr->to_string();
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
            log << "Invalid value '" << ip << "' for key '" << field << "' in ["
                << section.name() << "]: " << e.what();
            Throw<std::exception>();
        }
    }
}

void
parse_Port(ParsedPort& port, Section const& section, std::ostream& log)
{
    {
        auto const optResult = section.get("ip");
        if (optResult)
        {
            try
            {
                port.ip = boost::asio::ip::address::from_string(*optResult);
            }
            catch (std::exception const&)
            {
                log << "Invalid value '" << *optResult << "' for key 'ip' in ["
                    << section.name() << "]";
                Rethrow();
            }
        }
    }

    {
        auto const optResult = section.get("port");
        if (optResult)
        {
            try
            {
                port.port = beast::lexicalCastThrow<std::uint16_t>(*optResult);

                // Port 0 is not supported
                if (*port.port == 0)
                    Throw<std::exception>();
            }
            catch (std::exception const&)
            {
                log << "Invalid value '" << *optResult << "' for key "
                    << "'port' in [" << section.name() << "]";
                Rethrow();
            }
        }
    }

    {
        auto const optResult = section.get("protocol");
        if (optResult)
        {
            for (auto const& s : beast::rfc2616::split_commas(
                     optResult->begin(), optResult->end()))
                port.protocol.insert(s);
        }
    }

    {
        auto const lim = get(section, "limit", "unlimited");

        if (!boost::iequals(lim, "unlimited"))
        {
            try
            {
                port.limit =
                    safe_cast<int>(beast::lexicalCastThrow<std::uint16_t>(lim));
            }
            catch (std::exception const&)
            {
                log << "Invalid value '" << lim << "' for key "
                    << "'limit' in [" << section.name() << "]";
                Rethrow();
            }
        }
    }

    {
        auto const optResult = section.get("send_queue_limit");
        if (optResult)
        {
            try
            {
                port.ws_queue_limit =
                    beast::lexicalCastThrow<std::uint16_t>(*optResult);

                // Queue must be greater than 0
                if (port.ws_queue_limit == 0)
                    Throw<std::exception>();
            }
            catch (std::exception const&)
            {
                log << "Invalid value '" << *optResult << "' for key "
                    << "'send_queue_limit' in [" << section.name() << "]";
                Rethrow();
            }
        }
        else
        {
            // Default Websocket send queue size limit
            port.ws_queue_limit = 100;
        }
    }

    populate(section, "admin", log, port.admin_nets_v4, port.admin_nets_v6);
    populate(
        section,
        "secure_gateway",
        log,
        port.secure_gateway_nets_v4,
        port.secure_gateway_nets_v6);

    set(port.user, "user", section);
    set(port.password, "password", section);
    set(port.admin_user, "admin_user", section);
    set(port.admin_password, "admin_password", section);
    set(port.ssl_key, "ssl_key", section);
    set(port.ssl_cert, "ssl_cert", section);
    set(port.ssl_chain, "ssl_chain", section);
    set(port.ssl_ciphers, "ssl_ciphers", section);

    port.pmd_options.server_enable =
        section.value_or("permessage_deflate", true);
    port.pmd_options.client_max_window_bits =
        section.value_or("client_max_window_bits", 15);
    port.pmd_options.server_max_window_bits =
        section.value_or("server_max_window_bits", 15);
    port.pmd_options.client_no_context_takeover =
        section.value_or("client_no_context_takeover", false);
    port.pmd_options.server_no_context_takeover =
        section.value_or("server_no_context_takeover", false);
    port.pmd_options.compLevel = section.value_or("compress_level", 8);
    port.pmd_options.memLevel = section.value_or("memory_level", 4);
}

}  // namespace ripple
