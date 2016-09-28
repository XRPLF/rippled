//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NUDB_FIELD_HPP
#define NUDB_FIELD_HPP

#include <nudb/detail/stream.hpp>
#include <boost/assert.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace nudb {
namespace detail {

// A 24-bit integer
struct uint24_t;

// A 48-bit integer
struct uint48_t;

// These metafunctions describe the binary format of fields on disk

template<class T>
struct field;

template<>
struct field<std::uint8_t>
{
    static std::size_t constexpr size = 1;
    static std::uint64_t constexpr max = 0xff;
};

template<>
struct field<std::uint16_t>
{
    static std::size_t constexpr size = 2;
    static std::uint64_t constexpr max = 0xffff;
};

template<>
struct field<uint24_t>
{
    static std::size_t constexpr size = 3;
    static std::uint64_t constexpr max = 0xffffff;
};

template<>
struct field<std::uint32_t>
{
    static std::size_t constexpr size = 4;
    static std::uint64_t constexpr max = 0xffffffff;
};

template<>
struct field<uint48_t>
{
    static std::size_t constexpr size = 6;
    static std::uint64_t constexpr max = 0x0000ffffffffffff;
};

template<>
struct field<std::uint64_t>
{
    static std::size_t constexpr size = 8;
    static std::uint64_t constexpr max = 0xffffffffffffffff;
};

// read field from memory

template<class T, class U, typename std::enable_if<
    std::is_same<T, std::uint8_t>::value>::type* = nullptr>
void
readp(void const* v, U& u)
{
    auto p = reinterpret_cast<std::uint8_t const*>(v);
    u = *p;
}

template<class T, class U, typename std::enable_if<
    std::is_same<T, std::uint16_t>::value>::type* = nullptr>
void
readp(void const* v, U& u)
{
    auto p = reinterpret_cast<std::uint8_t const*>(v);
    T t;
    t =  T(*p++)<< 8;
    t =  T(*p  )      | t;
    u = t;
}

template<class T, class U, typename std::enable_if<
    std::is_same<T, uint24_t>::value>::type* = nullptr>
void
readp(void const* v, U& u)
{
    auto p = reinterpret_cast<std::uint8_t const*>(v);
    std::uint32_t t;
    t =  std::uint32_t(*p++)<<16;
    t = (std::uint32_t(*p++)<< 8) | t;
    t =  std::uint32_t(*p  )      | t;
    u = t;
}

template<class T, class U, typename std::enable_if<
    std::is_same<T, std::uint32_t>::value>::type* = nullptr>
void
readp(void const* v, U& u)
{
    auto const* p = reinterpret_cast<std::uint8_t const*>(v);
    T t;
    t =  T(*p++)<<24;
    t = (T(*p++)<<16) | t;
    t = (T(*p++)<< 8) | t;
    t =  T(*p  )      | t;
    u = t;
}

template<class T, class U, typename std::enable_if<
    std::is_same<T, uint48_t>::value>::type* = nullptr>
void
readp(void const* v, U& u)
{
    auto p = reinterpret_cast<std::uint8_t const*>(v);
    std::uint64_t t;
    t = (std::uint64_t(*p++)<<40);
    t = (std::uint64_t(*p++)<<32) | t;
    t = (std::uint64_t(*p++)<<24) | t;
    t = (std::uint64_t(*p++)<<16) | t;
    t = (std::uint64_t(*p++)<< 8) | t;
    t =  std::uint64_t(*p  )      | t;
    u = t;
}

template<class T, class U, typename std::enable_if<
    std::is_same<T, std::uint64_t>::value>::type* = nullptr>
void
readp(void const* v, U& u)
{
    auto p = reinterpret_cast<std::uint8_t const*>(v);
    T t;
    t =  T(*p++)<<56;
    t = (T(*p++)<<48) | t;
    t = (T(*p++)<<40) | t;
    t = (T(*p++)<<32) | t;
    t = (T(*p++)<<24) | t;
    t = (T(*p++)<<16) | t;
    t = (T(*p++)<< 8) | t;
    t =  T(*p  )      | t;
    u = t;
}

// read field from istream

template<class T, class U>
void
read(istream& is, U& u)
{
    readp<T>(is.data(field<T>::size), u);
}

inline
void
read_size48(istream& is, std::size_t& u)
{
    std::uint64_t v;
    read<uint48_t>(is, v);
    BOOST_ASSERT(v <= std::numeric_limits<std::uint32_t>::max());
    u = static_cast<std::uint32_t>(v);
}

// write field to ostream

template<class T, class U, typename std::enable_if<
    std::is_same<T, std::uint8_t>::value>::type* = nullptr>
void
write(ostream& os, U u)
{
    BOOST_ASSERT(u <= field<T>::max);
    std::uint8_t* p = os.data(field<T>::size);
    *p = static_cast<std::uint8_t>(u);
}

template<class T, class U, typename std::enable_if<
    std::is_same<T, std::uint16_t>::value>::type* = nullptr>
void
write(ostream& os, U u)
{
    BOOST_ASSERT(u <= field<T>::max);
    auto const t = static_cast<T>(u);
    std::uint8_t* p = os.data(field<T>::size);
    *p++ = (t>> 8)&0xff;
    *p   =  t     &0xff;
}

template<class T, class U, typename std::enable_if<
    std::is_same<T, uint24_t>::value>::type* = nullptr>
void
write(ostream& os, U u)
{
    BOOST_ASSERT(u <= field<T>::max);
    auto const t = static_cast<std::uint32_t>(u);
    std::uint8_t* p = os.data(field<T>::size);
    *p++ = (t>>16)&0xff;
    *p++ = (t>> 8)&0xff;
    *p   =  t     &0xff;
}

template<class T, class U, typename std::enable_if<
    std::is_same<T, std::uint32_t>::value>::type* = nullptr>
void
write(ostream& os, U u)
{
    BOOST_ASSERT(u <= field<T>::max);
    auto const t = static_cast<T>(u);
    std::uint8_t* p = os.data(field<T>::size);
    *p++ = (t>>24)&0xff;
    *p++ = (t>>16)&0xff;
    *p++ = (t>> 8)&0xff;
    *p   =  t     &0xff;
}

template<class T, class U, typename std::enable_if<
    std::is_same<T, uint48_t>::value>::type* = nullptr>
void
write(ostream& os, U u)
{
    BOOST_ASSERT(u <= field<T>::max);
    auto const t = static_cast<std::uint64_t>(u);
    std::uint8_t* p = os.data(field<T>::size);
    *p++ = (t>>40)&0xff;
    *p++ = (t>>32)&0xff;
    *p++ = (t>>24)&0xff;
    *p++ = (t>>16)&0xff;
    *p++ = (t>> 8)&0xff;
    *p   =  t     &0xff;
}

template<class T, class U, typename std::enable_if<
    std::is_same<T, std::uint64_t>::value>::type* = nullptr>
void
write(ostream& os, U u)
{
    auto const t = static_cast<T>(u);
    std::uint8_t* p = os.data(field<T>::size);
    *p++ = (t>>56)&0xff;
    *p++ = (t>>48)&0xff;
    *p++ = (t>>40)&0xff;
    *p++ = (t>>32)&0xff;
    *p++ = (t>>24)&0xff;
    *p++ = (t>>16)&0xff;
    *p++ = (t>> 8)&0xff;
    *p   =  t     &0xff;
}

} // detail
} // nudb

#endif
