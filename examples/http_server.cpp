//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "http_async_server.hpp"
#include "http_sync_server.hpp"

#include <beast/test/sig_wait.hpp>
#include <boost/program_options.hpp>

#include <iostream>

int main(int ac, char const* av[])
{
    using namespace beast::http;
    namespace po = boost::program_options;
    po::options_description desc("Options");

    desc.add_options()
        ("root,r",      po::value<std::string>()->default_value("."),
                        "Set the root directory for serving files")
        ("port,p",      po::value<std::uint16_t>()->default_value(8080),
                        "Set the port number for the server")
        ("ip",          po::value<std::string>()->default_value("0.0.0.0"),
                        "Set the IP address to bind to, \"0.0.0.0\" for all")
        ("threads,n",   po::value<std::size_t>()->default_value(4),
                        "Set the number of threads to use")
        ("sync,s",      "Launch a synchronous server")
        ;
    po::variables_map vm;
    po::store(po::parse_command_line(ac, av, desc), vm);

    std::string root =  vm["root"].as<std::string>();

    std::uint16_t port = vm["port"].as<std::uint16_t>();

    std::string ip = vm["ip"].as<std::string>();

    std::size_t threads = vm["threads"].as<std::size_t>();

    bool sync = vm.count("sync") > 0;

    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using address_type = boost::asio::ip::address;

    endpoint_type ep{address_type::from_string(ip), port};

    if(sync)
    {
        http_sync_server server(ep, root);
        beast::test::sig_wait();
    }
    else
    {
        http_async_server server(ep, threads, root);
        beast::test::sig_wait();
    }
}
