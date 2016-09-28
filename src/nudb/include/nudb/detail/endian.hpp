//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NUDB_DETAIL_ENDIAN_HPP
#define NUDB_DETAIL_ENDIAN_HPP

#include <cstdint>
#include <type_traits>

namespace nudb {
namespace detail {

// This is a modified work, original implementation
// by Howard Hinnant <howard.hinnant@gmail.com>
//
// "This should be standardized" - Howard

// Endian provides answers to the following questions:
// 1.  Is this system big or little endian?
// 2.  Is the "desired endian" of some class or function the same as the
//     native endian?
enum class endian
{
#ifdef _MSC_VER
    big    = 1,
    little = 0,
    native = little
#else
    native = __BYTE_ORDER__,
    little = __ORDER_LITTLE_ENDIAN__,
    big    = __ORDER_BIG_ENDIAN__
#endif
};

using is_little_endian =
    std::integral_constant<bool,
        endian::native == endian::little>;

static_assert(
    endian::native == endian::little || endian::native == endian::big,
    "endian::native shall be one of endian::little or endian::big");

static_assert(
    endian::big != endian::little,
    "endian::big and endian::little shall have different values");

// The pepper got baked into the file format as
// the hash of the little endian salt so now we
// need this function.
//
template<class = void>
std::uint64_t
to_little_endian(std::uint64_t v, std::false_type)
{
    union U
    {
        std::uint64_t vi;
        std::uint8_t va[8];
    };
    U u;
    u.va[0] =  v & 0xff;
    u.va[1] = (v >>  8) & 0xff;
    u.va[2] = (v >> 16) & 0xff;
    u.va[3] = (v >> 24) & 0xff;
    u.va[4] = (v >> 32) & 0xff;
    u.va[5] = (v >> 40) & 0xff;
    u.va[6] = (v >> 48) & 0xff;
    u.va[7] = (v >> 56) & 0xff;
    return u.vi;
}

inline
std::uint64_t
to_little_endian(std::uint64_t v, std::true_type)
{
    return v;
}

inline
std::uint64_t
to_little_endian(std::uint64_t v)
{
    return to_little_endian(v, is_little_endian{});
}

} // detail
} // nudb

#endif
