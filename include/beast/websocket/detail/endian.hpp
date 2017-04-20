//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_DETAIL_ENDIAN_HPP
#define BEAST_WEBSOCKET_DETAIL_ENDIAN_HPP

#include <cstdint>

namespace beast {
namespace websocket {
namespace detail {

inline
std::uint16_t
big_uint16_to_native(void const* buf)
{
    auto const p = reinterpret_cast<
        std::uint8_t const*>(buf);
    return (p[0]<<8) + p[1];
}

inline
std::uint64_t
big_uint64_to_native(void const* buf)
{
    auto const p = reinterpret_cast<
        std::uint8_t const*>(buf);
    return
        (static_cast<std::uint64_t>(p[0])<<56) +
        (static_cast<std::uint64_t>(p[1])<<48) +
        (static_cast<std::uint64_t>(p[2])<<40) +
        (static_cast<std::uint64_t>(p[3])<<32) +
        (static_cast<std::uint64_t>(p[4])<<24) +
        (static_cast<std::uint64_t>(p[5])<<16) +
        (static_cast<std::uint64_t>(p[6])<< 8) +
                                    p[7];
}

inline
std::uint32_t
little_uint32_to_native(void const* buf)
{
    auto const p = reinterpret_cast<
        std::uint8_t const*>(buf);
    return
                                    p[0] +
        (static_cast<std::uint32_t>(p[1])<< 8) +
        (static_cast<std::uint32_t>(p[2])<<16) +
        (static_cast<std::uint32_t>(p[3])<<24);
}

inline
void
native_to_little_uint32(std::uint32_t v, void* buf)
{
    auto p = reinterpret_cast<std::uint8_t*>(buf);
    p[0] =  v        & 0xff;
    p[1] = (v >>  8) & 0xff;
    p[2] = (v >> 16) & 0xff;
    p[3] = (v >> 24) & 0xff;
}

} // detail
} // websocket
} // beast

#endif
