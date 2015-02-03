//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2014, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_NUDB_FIELD_H_INCLUDED
#define BEAST_NUDB_FIELD_H_INCLUDED

#include <beast/nudb/detail/stream.h>
#include <beast/config/CompilerConfig.h> // for BEAST_CONSTEXPR
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <beast/cxx14/type_traits.h> // <type_traits>

namespace beast {
namespace nudb {
namespace detail {

// A 24-bit integer
struct uint24_t;

// A 48-bit integer
struct uint48_t;

// These metafunctions describe the binary format of fields on disk

template <class T>
struct field;

template <>
struct field <std::uint8_t>
{
    static std::size_t BEAST_CONSTEXPR size = 1;
    static std::size_t BEAST_CONSTEXPR max = 0xff;
};

template <>
struct field <std::uint16_t>
{
    static std::size_t BEAST_CONSTEXPR size = 2;
    static std::size_t BEAST_CONSTEXPR max = 0xffff;
};

template <>
struct field <uint24_t>
{
    static std::size_t BEAST_CONSTEXPR size = 3;
    static std::size_t BEAST_CONSTEXPR max = 0xffffff;
};

template <>
struct field <std::uint32_t>
{
    static std::size_t BEAST_CONSTEXPR size = 4;
    static std::size_t BEAST_CONSTEXPR max = 0xffffffff;
};

template <>
struct field <uint48_t>
{
    static std::size_t BEAST_CONSTEXPR size = 6;
    static std::size_t BEAST_CONSTEXPR max = 0x0000ffffffffffff;
};

template <>
struct field <std::uint64_t>
{
    static std::size_t BEAST_CONSTEXPR size = 8;
    static std::size_t BEAST_CONSTEXPR max = 0xffffffffffffffff;
};

// read field from memory

template <class T, class U, std::enable_if_t<
    std::is_same<T, std::uint8_t>::value>* = nullptr>
void
readp (void const* v, U& u)
{
    std::uint8_t const* p =
        reinterpret_cast<std::uint8_t const*>(v);
    u = *p;
}

template <class T, class U, std::enable_if_t<
    std::is_same<T, std::uint16_t>::value>* = nullptr>
void
readp (void const* v, U& u)
{
    std::uint8_t const* p =
        reinterpret_cast<std::uint8_t const*>(v);
    T t;
    t =  T(*p++)<< 8;
    t =  T(*p  )      | t;
    u = t;
}

template <class T, class U, std::enable_if_t<
    std::is_same<T, uint24_t>::value>* = nullptr>
void
readp (void const* v, U& u)
{
    std::uint8_t const* p =
        reinterpret_cast<std::uint8_t const*>(v);
    std::uint32_t t;
    t =  std::uint32_t(*p++)<<16;
    t = (std::uint32_t(*p++)<< 8) | t;
    t =  std::uint32_t(*p  )      | t;
    u = t;
}

template <class T, class U, std::enable_if_t<
    std::is_same<T, std::uint32_t>::value>* = nullptr>
void
readp (void const* v, U& u)
{
    std::uint8_t const* p =
        reinterpret_cast<std::uint8_t const*>(v);
    T t;
    t =  T(*p++)<<24;
    t = (T(*p++)<<16) | t;
    t = (T(*p++)<< 8) | t;
    t =  T(*p  )      | t;
    u = t;
}

template <class T, class U, std::enable_if_t<
    std::is_same<T, uint48_t>::value>* = nullptr>
void
readp (void const* v, U& u)
{
    std::uint8_t const* p =
        reinterpret_cast<std::uint8_t const*>(v);
    std::uint64_t t;
    t = (std::uint64_t(*p++)<<40);
    t = (std::uint64_t(*p++)<<32) | t;
    t = (std::uint64_t(*p++)<<24) | t;
    t = (std::uint64_t(*p++)<<16) | t;
    t = (std::uint64_t(*p++)<< 8) | t;
    t =  std::uint64_t(*p  )      | t;
    u = t;
}

template <class T, class U, std::enable_if_t<
    std::is_same<T, std::uint64_t>::value>* = nullptr>
void
readp (void const* v, U& u)
{
    std::uint8_t const* p =
        reinterpret_cast<std::uint8_t const*>(v);
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

template <class T, class U>
void
read (istream& is, U& u)
{
    readp<T>(is.data(field<T>::size), u);
}

// write field to ostream

template <class T, class U, std::enable_if_t<
    std::is_same<T, std::uint8_t>::value>* = nullptr>
void
write (ostream& os, U const& u)
{
    std::uint8_t* p =
        os.data(field<T>::size);
    *p = u;
}

template <class T, class U, std::enable_if_t<
    std::is_same<T, std::uint16_t>::value>* = nullptr>
void
write (ostream& os, U const& u)
{
    T t = u;
    std::uint8_t* p =
        os.data(field<T>::size);
    *p++ = (t>> 8)&0xff;
    *p   =  t     &0xff;
}

template <class T, class U, std::enable_if_t<
    std::is_same<T, uint24_t>::value>* = nullptr>
void
write (ostream& os, U const& u)
{
    T t = u;
    std::uint8_t* p =
        os.data(field<T>::size);
    *p++ = (t>>16)&0xff;
    *p++ = (t>> 8)&0xff;
    *p   =  t     &0xff;
}

template <class T, class U, std::enable_if_t<
    std::is_same<T, std::uint32_t>::value>* = nullptr>
void
write (ostream& os, U const& u)
{
    T t = u;
    std::uint8_t* p =
        os.data(field<T>::size);
    *p++ = (t>>24)&0xff;
    *p++ = (t>>16)&0xff;
    *p++ = (t>> 8)&0xff;
    *p   =  t     &0xff;
}

template <class T, class U, std::enable_if_t<
    std::is_same<T, uint48_t>::value>* = nullptr>
void
write (ostream& os, U const& u)
{
    std::uint64_t const t = u;
    std::uint8_t* p =
        os.data(field<T>::size);
    *p++ = (t>>40)&0xff;
    *p++ = (t>>32)&0xff;
    *p++ = (t>>24)&0xff;
    *p++ = (t>>16)&0xff;
    *p++ = (t>> 8)&0xff;
    *p   =  t     &0xff;
}

template <class T, class U, std::enable_if_t<
    std::is_same<T, std::uint64_t>::value>* = nullptr>
void
write (ostream& os, U const& u)
{
    T t = u;
    std::uint8_t* p =
        os.data(field<T>::size);
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
} // beast

#endif
