//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "websocket_async_echo_server.hpp"
#include "websocket_sync_echo_server.hpp"
#include <beast/test/sig_wait.hpp>
#include <iostream>

int main()
{
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using address_type = boost::asio::ip::address;

    try
    {
        boost::system::error_code ec;
    	beast::websocket::async_echo_server s1{nullptr, 1};
        s1.open(true, endpoint_type{
            address_type::from_string("0.0.0.0"), 6000 }, ec);

    	beast::websocket::sync_echo_server s2(true, endpoint_type{
	        address_type::from_string("0.0.0.0"), 6001 });

    	beast::test::sig_wait();
    }
    catch(std::exception const& e)
    {
    	std::cout << "Error: " << e.what() << std::endl;
    }
}
