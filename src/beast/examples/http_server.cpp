//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#include "http_async_server.hpp"
#include "http_sync_server.hpp"
#include "../test/sig_wait.hpp"

#include <boost/program_options.hpp>

#include <iostream>

int main(int ac, char const* av[])
{
    using namespace beast::http;
    namespace po = boost::program_options;
    po::options_description desc("Options");

    desc.add_options()
        ("root,r",      po::value<std::string>()->implicit_value("."),
                        "Set the root directory for serving files")
        ("port,p",      po::value<std::uint16_t>()->implicit_value(8080),
                        "Set the port number for the server")
        ("ip",          po::value<std::string>()->implicit_value("0.0.0.0"),
                        "Set the IP address to bind to, \"0.0.0.0\" for all")
        ("threads,n",   po::value<std::size_t>()->implicit_value(4),
                        "Set the number of threads to use")
        ("sync,s",      "Launch a synchronous server")
        ;
    po::variables_map vm;
    po::store(po::parse_command_line(ac, av, desc), vm);

    std::string root = ".";
    if(vm.count("root"))
        root = vm["root"].as<std::string>();

    std::uint16_t port = 8080;
    if(vm.count("port"))
        port = vm["port"].as<std::uint16_t>();

    std::string ip = "0.0.0.0";
    if(vm.count("ip"))
        ip = vm["ip"].as<std::string>();

    std::size_t threads = 4;
    if(vm.count("threads"))
        threads = vm["threads"].as<std::size_t>();

    bool sync = vm.count("sync") > 0;

    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using address_type = boost::asio::ip::address;

    endpoint_type ep{address_type::from_string(ip), port};

    if(sync)
        http_sync_server server(ep, root);
    else
        http_async_server server(ep, threads, root);
    sig_wait();
}
