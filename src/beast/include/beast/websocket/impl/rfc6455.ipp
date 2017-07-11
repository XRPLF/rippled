//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_IMPL_RFC6455_IPP
#define BEAST_WEBSOCKET_IMPL_RFC6455_IPP

#include <beast/http/fields.hpp>
#include <beast/http/rfc7230.hpp>

namespace beast {
namespace websocket {

template<class Allocator>
bool
is_upgrade(http::header<true,
    http::basic_fields<Allocator>> const& req)
{
    if(req.version < 11)
        return false;
    if(req.method() != http::verb::get)
        return false;
    if(! http::token_list{req["Connection"]}.exists("upgrade"))
        return false;
    if(! http::token_list{req["Upgrade"]}.exists("websocket"))
        return false;
    if(! req.count(http::field::sec_websocket_version))
        return false;
    return true;
}

} // websocket
} // beast

#endif
