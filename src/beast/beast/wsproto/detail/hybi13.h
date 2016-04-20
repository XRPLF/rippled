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

#ifndef BEAST_WSPROTO_HYBI13_H_INCLUDED
#define BEAST_WSPROTO_HYBI13_H_INCLUDED

#include <beast/detail/base64.hpp>
#include <beast/detail/sha1.hpp>
#include <boost/utility/string_ref.hpp>
#include <cstdint>
#include <string>
#include <type_traits>

namespace beast {
namespace wsproto {
namespace detail {

template<class Gen>
std::string
make_sec_ws_key(Gen& g)
{
    union U
    {
        std::array<std::uint32_t, 4> a4;
        std::array<std::uint8_t, 16> a16;
    };
    U u;
    for(int i = 0; i < 4; ++i)
        u.a4[i] = g();
    return beast::detail::base64_encode(u.a16.data(), u.a16.size());
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
} // wsproto
} // beast

#endif
