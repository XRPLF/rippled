//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_EXAMPLE_SERVER_FRAMEWORK_HPP
#define BEAST_EXAMPLE_SERVER_FRAMEWORK_HPP

#include <boost/asio/io_service.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/system/error_code.hpp>
#include <utility>

/** The framework namespace

    This namespace contains all of the identifiers in the
    server-framework system. Here we import some commonly
    used types for brevity.
*/
namespace framework {

// This is our own base from member idiom written for C++11
// which is simple and works around a glitch in boost's version.
//
template<class T>
class base_from_member
{
public:
    template<class... Args>
    explicit
    base_from_member(Args&&... args)
        : member(std::forward<Args>(args)...)
    {
    }

protected:
    T member;
};

using error_code = boost::system::error_code;
using socket_type = boost::asio::ip::tcp::socket;
using strand_type = boost::asio::io_service::strand;
using address_type = boost::asio::ip::address_v4;
using endpoint_type = boost::asio::ip::tcp::endpoint;
using acceptor_type = boost::asio::ip::tcp::acceptor;
using io_service_type = boost::asio::io_service;

} // framework

#endif
