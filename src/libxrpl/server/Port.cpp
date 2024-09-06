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

#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/beast/rfc2616.h>
#include <xrpl/server/Port.h>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <sstream>

namespace ripple {

bool Port::secure() const {
    return protocol.count("peer") > 0 || protocol.count("https") > 0 ||
           protocol.count("wss") > 0 || protocol.count("wss2") > 0;
}

std::string Port::protocols() const {
    std::string s;
    for (auto iter = protocol.cbegin(); iter != protocol.cend(); ++iter)
        s += (iter != protocol.cbegin() ? "," : "") + *iter;
    return s;
}

std::ostream& operator<<(std::ostream& os, Port const& p) {
    os << "'" << p.name << "' (ip=" << p.ip << ":" << p.port << ", ";

    if (p.admin_nets_v4.size() || p.admin_nets_v6.size()) {
        os << "admin nets:";
        for (auto const& net : p.admin_nets_v4) {
            os << net.to_string();
            os << ", ";
        }
        for (auto const& net : p.admin_nets_v6) {
            os << net.to_string();
            os << ", ";
        }
    }

    if (p.secure_gateway_nets_v4.size() || p.secure_gateway_nets_v6.size()) {
        os << "secure_gateway nets:";
        for (auto const& net : p.secure_gateway_nets_v4) {
            os << net.to_string();
            os << ", ";
        }
        for (auto const& net : p.secure_gateway_nets_v6) {
            os << net.to_string();
            os << ", ";
        }
    }

    os << p.protocols() << ")";
    return os;
}

//------------------------------------------------------------------------------

static void populate(
    Section const& section,
    std::string const& field,
    std::ostream& log,
    std::vector<boost::asio::ip::network_v4>& nets4,
    std::vector<boost::asio::ip::network_v6>& nets6) {
    auto const optResult = section.get(field);
    if (!optResult)
        return;

    std::stringstream ss(*optResult);
    std::string ip;

    while (std::getline(ss, ip, ',')) {
        boost::algorithm::trim(ip);
        bool v4;
        boost::asio::ip::network_v4 v4Net;
        boost::asio::ip::network_v6 v6Net;

        try {
            auto const addr = beast::IP::Endpoint::from_string_checked(ip);
            if (addr) {
                if (is_unspecified(*addr)) {
                    nets4.push_back(
                        boost::asio::ip::make_network_v4("0.0.0.0/0"));
                    nets6.push_back(boost::asio::ip::make_network_v6("::/0"));
                    break;
                }

                v4 = addr->is_v4();
                std::string addressString = addr->to_string();
                if (v4) {
                    addressString += "/32";
                    v4Net = boost::asio::ip::make_network_v4(addressString);
                } else {
                    addressString += "/128";
                    v6Net = boost::asio::ip::make_network_v6(addressString);
                }
            } else {
                try {
                    v4Net = boost::asio::ip::make_network_v4(ip);
                    v4 = true;
                } catch (boost::system::system_error const&) {
                    v6Net = boost::asio::ip::makenetwork_v6(ip);
                    v4 = false;
                }
            }

            if (v4) {
                if (v4Net != v4Net.canonical()) {
                    log << "The configured subnet " << v4Net.to_string()
                        << " is not the same as the network address, which is "
                        << v4Net.canonical().to_string();
                    Throw<std::exception>();
                }
                nets4.push_back(v4Net);
            } else {
                if (v6Net != v6Net.canonical()) {
                    log << "The configured subnet " << v6Net.to_string()
                        << " is not the same as the network address, which is "
                        << v6Net.canonical().to_string();
                    Throw<std::exception>();
                }
                nets6.push_back(v6Net);
            }
        } catch (boost::system::system_error const& e) {
            log << "Invalid subnet configuration for key '" << field << "' in ["
                << section.name() << "]: " << e.what();
            Throw<std::exception>();
        }
    }
}

void parse_Port(ParsedPort& port, Section const& section, std::ostream& log) {
    {
        auto const optResult = section.get("ip");
        if (optResult) {
            try {
                port.ip = boost::asio::ip::address::from_string(*optResult);
            } catch (std::exception const&) {
                log << "Invalid IP address configuration for key 'ip' in ["
                    << section.name() << "]";
                Rethrow();
            }
        }
    }

    {
        auto const optResult = section.get("port");
        if (optResult) {
            try {
                port.port = beast::lexicalCastThrow<std::uint16_t>(*optResult);

                if (*port.port == 0)
                    Throw<std::exception>();
            } catch (std::exception const&) {
                log << "Invalid port configuration for key 'port' in ["
                    << section.name() << "]";
                Rethrow();
            }
        }
    }

    {
        auto const optResult = section.get("protocol");
        if (optResult) {
            for (auto const& s : beast::rfc2616::split_commas(
                     optResult->begin(), optResult->end()))
                port.protocol.insert(s);
        }
    }

    {
        auto const lim = get(section, "limit", "unlimited");

        if (!boost::iequals(lim, "unlimited")) {
            try {
                port.limit =
                    safe_cast<int>(beast::lexicalCastThrow<std::uint16_t>(lim));
            } catch (std::exception const&) {
                log << "Invalid limit configuration for key 'limit' in ["
                    << section.name() << "]";
                Rethrow();
            }
        }
    }

    {
        auto const optResult = section.get("send_queue_limit");
        if (optResult) {
            try {
                port.ws_queue_limit =
                    beast::lexicalCastThrow<std::uint16_t>(*optResult);

                if (port.ws_queue_limit == 0)
                    Throw<std::exception>();
            } catch (std::exception const&) {
                log << "Invalid send queue limit configuration for key "
                    << "'send_queue_limit' in [" << section.name() << "]";
                Rethrow();
            }
        } else {
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

} // namespace ripple
