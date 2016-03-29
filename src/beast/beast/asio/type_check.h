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

#ifndef BEAST_WSPROTO_TYPE_CHECK_H_INCLUDED
#define BEAST_WSPROTO_TYPE_CHECK_H_INCLUDED

#include <beast/is_call_possible.h>
#include <boost/asio/buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_service.hpp>
#include <iterator>
#include <type_traits>
#include <utility>

namespace beast {
namespace asio {

template<class C>
class has_get_io_service
{
    template<class T, class R = typename std::is_same<
        decltype(std::declval<T>().get_io_service()),
            boost::asio::io_service&>>
    static R check(int);
    template <class>
    static std::false_type check(...);
    using type = decltype(check<C>(0));

public:
    static bool const value = type::value;
};
static_assert(! has_get_io_service<int>::value, "");

// http://www.boost.org/doc/libs/1_60_0/doc/html/boost_asio/reference/AsyncReadStream.html
//
template<class C>
class is_AsyncReadStream
{
    struct Handler{};
    struct Buffers{};

    template<class T, class R = decltype(
        std::declval<T>().async_read_some(
            std::declval<Buffers>(), std::declval<Handler>()),
                std::true_type{})>
    static R check(int);
    template<class>
    static std::false_type check(...);
    using type = decltype(check<C>(0));

public:
    static bool const value =
        has_get_io_service<C>::value && type::value;
};
static_assert(! is_AsyncReadStream<int>::value, "");

// http://www.boost.org/doc/libs/1_60_0/doc/html/boost_asio/reference/AsyncWriteStream.html
//
template<class C>
class is_AsyncWriteStream
{
    struct Handler{};
    struct Buffers{};

    template<class T, class R = decltype(
        std::declval<T>().async_write_some(
            std::declval<Buffers>(), std::declval<Handler>()),
                std::true_type{})>
    static R check(int);
    template<class>
    static std::false_type check(...);
    using type = decltype(check<C>(0));

public:
    static bool const value =
        has_get_io_service<C>::value && type::value;
};
static_assert(! is_AsyncWriteStream<int>::value, "");

// http://www.boost.org/doc/libs/1_60_0/doc/html/boost_asio/reference/SyncReadStream.html
//
template<class C>
class is_SyncReadStream
{
    using error_code =
        boost::system::error_code;
    struct Buffers{};

    template<class T, class R = std::is_same<decltype(
        std::declval<T>().read_some(std::declval<Buffers>())),
            std::size_t>>
    static R check1(int);
    template<class>
    static std::false_type check1(...);
    using type1 = decltype(check1<C>(0));

    template<class T, class R = std::is_same<decltype(
        std::declval<T>().read_some(
            std::declval<Buffers>(), std::declval<error_code&>())),
                std::size_t>>
    static R check2(int);
    template<class>
    static std::false_type check2(...);
    using type2 = decltype(check2<C>(0));

public:
    static bool const value =
        type1::value && type2::value;
};
static_assert(! is_SyncReadStream<int>::value, "");

// http://www.boost.org/doc/libs/1_60_0/doc/html/boost_asio/reference/SyncWriteStream.html
//
template<class C>
class is_SyncWriteStream
{
    using error_code =
        boost::system::error_code;
    struct Buffers{};

    template<class T, class R = std::is_same<decltype(
        std::declval<T>().write_some(std::declval<Buffers>())),
            std::size_t>>
    static R check1(int);
    template<class>
    static std::false_type check1(...);
    using type1 = decltype(check1<C>(0));

    template<class T, class R = std::is_same<decltype(
        std::declval<T>().write_some(
            std::declval<Buffers>(), std::declval<error_code&>())),
                std::size_t>>
    static R check2(int);
    template<class>
    static std::false_type check2(...);
    using type2 = decltype(check2<C>(0));

public:
    static bool const value =
        type1::value && type2::value;
};
static_assert(! is_SyncWriteStream<int>::value, "");

template<class C>
struct is_Stream
{
    static bool const value =
        is_AsyncReadStream<C>::value &&
        is_AsyncWriteStream<C>::value &&
        is_SyncReadStream<C>::value &&
        is_SyncWriteStream<C>::value;
};

// http://www.boost.org/doc/libs/1_60_0/doc/html/boost_asio/reference/ConstBufferSequence.html
// http://www.boost.org/doc/libs/1_60_0/doc/html/boost_asio/reference/MutableBufferSequence.html
//
template<class C, class BufferType>
class is_BufferSequence
{
    template<class T, class R = std::is_convertible<
        typename T::value_type, BufferType> >
    static R check1(int);
    template<class>
    static std::false_type check1(...);
    using type1 = decltype(check1<C>(0));

    template<class T, class R = std::is_base_of<
    #if 0
        std::bidirectional_iterator_tag,
            typename std::iterator_traits<
                typename T::const_iterator>::iterator_category>>
    #else
        // workaround:
        // boost::asio::detail::consuming_buffers::const_iterator
        // is not bidirectional
        std::forward_iterator_tag,
            typename std::iterator_traits<
                typename T::const_iterator>::iterator_category>>
    #endif
    static R check2(int);
    template<class>
    static std::false_type check2(...);
    using type2 = decltype(check2<C>(0));

    template<class T, class R = typename
        std::is_convertible<decltype(
            std::declval<T>().begin()),
                typename T::const_iterator>::type>
    static R check3(int);
    template<class>
    static std::false_type check3(...);
    using type3 = decltype(check3<C>(0));

    template<class T, class R = typename std::is_convertible<decltype(
        std::declval<T>().end()),
            typename T::const_iterator>::type>
    static R check4(int);
    template<class>
    static std::false_type check4(...);
    using type4 = decltype(check4<C>(0));

public:
    static bool const value =
        std::is_copy_constructible<C>::value &&
        std::is_destructible<C>::value &&
        type1::value && type2::value &&
        type3::value && type4::value;
};

template<class C>
using is_ConstBufferSequence =
    is_BufferSequence<C, boost::asio::const_buffer>;

template<class C>
using is_MutableBufferSequence =
    is_BufferSequence<C, boost::asio::mutable_buffer>;

static_assert(! is_ConstBufferSequence<int>::value, "");
static_assert(! is_MutableBufferSequence<int>::value, "");

template<class C>
class is_Streambuf
{
    template<class T, class R = std::integral_constant<bool,
        is_MutableBufferSequence<
            decltype(std::declval<T>().prepare(1))>::value>>
    static R check1(int);
    template<class>
    static std::false_type check1(...);
    using type1 = decltype(check1<C>(0));

    template<class T, class R = std::integral_constant<bool,
        is_ConstBufferSequence<
            decltype(std::declval<T>().data())>::value>>
    static R check2(int);
    template<class>
    static std::false_type check2(...);
    using type2 = decltype(check2<C>(0));

    template<class T, class R = decltype(
        std::declval<T>().commit(1),
            std::true_type{})>
    static R check3(int);
    template<class>
    static std::false_type check3(...);
    using type3 = decltype(check3<C>(0));

    template<class T, class R = decltype(
        std::declval<T>().consume(1),
            std::true_type{})>
    static R check4(int);
    template<class>
    static std::false_type check4(...);
    using type4 = decltype(check4<C>(0));

    template<class T, class R = std::is_same<
        decltype(std::declval<T>().size()), std::size_t>>
    static R check5(int);
    template<class>
    static std::false_type check5(...);
    using type5 = decltype(check5<C>(0));

public:
    static bool const value =
        type1::value && type2::value && type3::value &&
        type4::value && type5::value;
};
static_assert(! is_Streambuf<int>::value, "");

// VFALCO TODO Use boost::asio::handler_type
template<class Handler, class Signature>
using is_Handler = std::integral_constant<bool,
    std::is_copy_constructible<std::decay_t<Handler>>::value &&
        is_call_possible<Handler, Signature>::value>;

} // asio
} // beast

#endif
