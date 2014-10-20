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

#include <ripple/http/Server.h>
#include <ripple/http/impl/ServerImpl.h>
#include <beast/cxx14/memory.h> // <memory>
#include <beast/cxx14/algorithm.h> // <algorithm>
#include <boost/regex.hpp>
#include <stdexcept>

namespace ripple {
namespace HTTP {

std::unique_ptr<Server>
make_Server (Handler& handler, beast::Journal journal)
{
    return std::make_unique<ServerImpl>(handler, journal);
}

//------------------------------------------------------------------------------

/** Parse a comma-delimited list of values. */
std::vector<std::string>
parse_csv (std::string const& in, std::ostream& log)
{
    auto first = in.cbegin();
    auto const last = in.cend();
    std::vector<std::string> result;
    if (first != last)
    {
        static boost::regex const re(
            "^"                         // start of line
            "(?:\\s*)"                  // whitespace (optional)
            "([a-zA-Z][_a-zA-Z0-9]*)"   // identifier
            "(?:\\s*)"                  // whitespace (optional)
            "(?:,?)"                    // comma (optional)
            "(?:\\s*)"                  // whitespace (optional)
            , boost::regex_constants::optimize
        );
        for(;;)
        {
            boost::smatch m;
            if (! boost::regex_search(first, last, m, re,
                boost::regex_constants::match_continuous))
            {
                log << "Expected <identifier>\n";
                throw std::exception();
            }
            result.push_back(m[1]);
            first = m[0].second;
            if (first == last)
                break;
        }
    }
    return result;
}

void
Port::parse (Port& port, Section const& section, std::ostream& log)
{
    {
        auto result = section.find("ip");
        if (result.second)
        {
            try
            {
                port.ip = boost::asio::ip::address::from_string(result.first);
            }
            catch(...)
            {
                log << "Invalid value '" << result.first <<
                    "' for key 'ip' in [" << section.name() << "]\n";
                throw std::exception();
            }
        }
    }

    {
        auto const result = section.find("port");
        if (result.second)
        {
            auto const ul = std::stoul(result.first);
            if (ul > std::numeric_limits<std::uint16_t>::max())
            {
                log <<
                    "Value '" << result.first << "' for key 'port' is out of range\n";
                throw std::exception();
            }
            if (ul == 0)
            {
                log <<
                    "Value '0' for key 'port' is invalid\n";
                throw std::exception();
            }
            port.port = static_cast<std::uint16_t>(ul);
        }
    }

    {
        auto const result = section.find("protocol");
        if (result.second)
        {
            for (auto const& s : parse_csv(result.first, log))
                port.protocols.insert(s);
        }
        else
        {
            if (port.protocols.empty())
            {
                log << "Required key 'protocol' missing from [" << section.name() << "]\n";
                throw std::exception();
            }
        }
    }

    {
        auto const result = section.find("admin");
        if (result.second)
        {
            if (result.first == "no")
            {
                port.allow_admin = false;
            }
            else if (result.first == "allow")
            {
                port.allow_admin = true;
            }
            else
            {
                log << "Invalid value '" << result.first <<
                    "' for key 'admin' in [" << section.name() << "]\n";
                throw std::exception();
            }
        }
    }

    set(port.ssl_key, "ssl_key", section);
    set(port.ssl_cert, "ssl_cert", section);
    set(port.ssl_chain, "ssl_chain", section);
}

std::vector<Port>
Server::parse (BasicConfig const& config, std::ostream& log)
{
    std::vector <Port> result;

    if (! config.exists("doors"))
    {
        log <<
            "Missing section: [doors]\n";
        return result;
    }

    Port common;
    Port::parse (common, config["doors"], log);

    auto const& names = config.section("doors").values();
    result.reserve(names.size());
    for (auto const& name : names)
    {
        if (! config.exists(name))
        {
            log <<
                "Missing section: [" << name << "]\n";
            throw std::exception();
        }
        result.push_back(common);
        result.back().name = name;
        Port::parse (result.back(), config[name], log);
    }

    std::size_t count = 0;
    for (auto const& p : result)
        if (p.protocols.count("peer") > 0)
            ++count;
    if (count > 1)
    {
        log << "Error: More than one peer protocol configured in [doors]\n";
        throw std::exception();
    }
    if (count == 0)
        log << "Warning: No peer protocol configured\n";

    for (auto const& p : result)
    {
        if (p.port == 0)
        {
            log << "Error: missing port for [" << p.name << "]\n";
            throw std::exception();
        }
    }
    return result;
}

}
}
