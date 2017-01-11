//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_DETAIL_HYBI13_HPP
#define BEAST_WEBSOCKET_DETAIL_HYBI13_HPP

#include <beast/core/detail/base64.hpp>
#include <beast/core/detail/sha1.hpp>
#include <boost/utility/string_ref.hpp>
#include <array>
#include <cstdint>
#include <string>
#include <type_traits>

namespace beast {
namespace websocket {
namespace detail {

template<class Gen>
std::string
make_sec_ws_key(Gen& g)
{
    std::array<std::uint8_t, 16> a;
    for(int i = 0; i < 16; i += 4)
    {
        auto const v = g();
        a[i  ] =  v        & 0xff;
        a[i+1] = (v >>  8) & 0xff;
        a[i+2] = (v >> 16) & 0xff;
        a[i+3] = (v >> 24) & 0xff;
    }
    return beast::detail::base64_encode(
        a.data(), a.size());
}

template<class = void>
std::string
make_sec_ws_accept(boost::string_ref const& key)
{
    std::string s(key.data(), key.size());
    s += "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    beast::detail::sha1_context ctx;
    beast::detail::init(ctx);
    beast::detail::update(ctx, s.data(), s.size());
    std::array<std::uint8_t,
        beast::detail::sha1_context::digest_size> digest;
    beast::detail::finish(ctx, digest.data());
    return beast::detail::base64_encode(
        digest.data(), digest.size());
}

} // detail
} // websocket
} // beast

#endif
