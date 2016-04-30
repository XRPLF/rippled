//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_TYPE_CHECK_HPP
#define BEAST_TYPE_CHECK_HPP

#include <beast/detail/is_call_possible.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_service.hpp>
#include <iterator>
#include <type_traits>
#include <utility>

namespace beast {

//------------------------------------------------------------------------------

// Types that meet the requirements,
// for use with std::declval only.
//

#if GENERATING_DOCS
namespace detail {
#else
namespace concept {
#endif

template<class BufferType>
struct BufferSequence
{
    using value_type = BufferType;
    using const_iterator = BufferType const*;
    ~BufferSequence();
    BufferSequence(BufferSequence const&) = default;
    const_iterator
    begin() const noexcept;
    const_iterator
    end() const noexcept;
};

using ConstBufferSequence =
    BufferSequence<boost::asio::const_buffer>;

using MutableBufferSequence =
    BufferSequence<boost::asio::mutable_buffer>;

struct StreamHandler
{
    StreamHandler(StreamHandler const&) = default;
    void
    operator()(boost::system::error_code ec,
        std::size_t);
};

using ReadHandler = StreamHandler;
using WriteHandler = StreamHandler;

} // concept

// http://www.boost.org/doc/libs/1_60_0/doc/html/boost_asio/reference/ConstBufferSequence.html
// http://www.boost.org/doc/libs/1_60_0/doc/html/boost_asio/reference/MutableBufferSequence.html
//
/// Determine if `T` meets the requirements of `BufferSequence`.
template<class T, class BufferType>
class is_BufferSequence
{
    template<class U, class R = std::is_convertible<
        typename U::value_type, BufferType> >
    static R check1(int);
    template<class>
    static std::false_type check1(...);
    using type1 = decltype(check1<T>(0));

    template<class U, class R = std::is_base_of<
    #if 0
        std::bidirectional_iterator_tag,
            typename std::iterator_traits<
                typename U::const_iterator>::iterator_category>>
    #else
        // workaround:
        // boost::asio::detail::consuming_buffers::const_iterator
        // is not bidirectional
        std::forward_iterator_tag,
            typename std::iterator_traits<
                typename U::const_iterator>::iterator_category>>
    #endif
    static R check2(int);
    template<class>
    static std::false_type check2(...);
    using type2 = decltype(check2<T>(0));

    template<class U, class R = typename
        std::is_convertible<decltype(
            std::declval<U>().begin()),
                typename U::const_iterator>::type>
    static R check3(int);
    template<class>
    static std::false_type check3(...);
    using type3 = decltype(check3<T>(0));

    template<class U, class R = typename std::is_convertible<decltype(
        std::declval<U>().end()),
            typename U::const_iterator>::type>
    static R check4(int);
    template<class>
    static std::false_type check4(...);
    using type4 = decltype(check4<T>(0));

public:
    /// `true` if `T` meets the requirements.
    static bool const value =
        std::is_copy_constructible<T>::value &&
        std::is_destructible<T>::value &&
        type1::value && type2::value &&
        type3::value && type4::value;
};

#if ! GENERATING_DOCS

/// Determine if `T` meets the requirements of `ConstBufferSequence`.
template<class T>
using is_ConstBufferSequence =
    is_BufferSequence<T, boost::asio::const_buffer>;
static_assert(is_ConstBufferSequence<concept::ConstBufferSequence>::value, "");
static_assert(! is_ConstBufferSequence<int>::value, "");

/// Determine if `T` meets the requirements of `MutableBufferSequence`.
template<class C>
using is_MutableBufferSequence =
    is_BufferSequence<C, boost::asio::mutable_buffer>;
static_assert(is_MutableBufferSequence<concept::MutableBufferSequence>::value, "");
static_assert(! is_MutableBufferSequence<int>::value, "");

#endif

//------------------------------------------------------------------------------

/// Determine if `T` has the `get_io_service` member.
template<class T>
class has_get_io_service
{
    template<class U, class R = typename std::is_same<
        decltype(std::declval<U>().get_io_service()),
            boost::asio::io_service&>>
    static R check(int);
    template <class>
    static std::false_type check(...);
    using type = decltype(check<T>(0));

public:
    /// `true` if `T` meets the requirements.
    static bool const value = type::value;
};
static_assert(! has_get_io_service<int>::value, "");

// http://www.boost.org/doc/libs/1_60_0/doc/html/boost_asio/reference/AsyncReadStream.html
//
/// Determine if `T` meets the requirements of `AsyncReadStream`.
template<class T>
class is_AsyncReadStream
{
    template<class U, class R = decltype(
        std::declval<U>().async_read_some(
            std::declval<concept::MutableBufferSequence>(),
                std::declval<concept::ReadHandler>()),
                    std::true_type{})>
    static R check(int);
    template<class>
    static std::false_type check(...);
    using type = decltype(check<T>(0));

public:
    /// `true` if `T` meets the requirements.
    static bool const value =
        has_get_io_service<T>::value && type::value;
};
static_assert(! is_AsyncReadStream<int>::value, "");

// http://www.boost.org/doc/libs/1_60_0/doc/html/boost_asio/reference/AsyncWriteStream.html
//
/// Determine if `T` meets the requirements of `AsyncWriteStream`.
template<class T>
class is_AsyncWriteStream
{
    template<class U, class R = decltype(
        std::declval<U>().async_write_some(
            std::declval<concept::ConstBufferSequence>(),
                std::declval<concept::WriteHandler>()),
                    std::true_type{})>
    static R check(int);
    template<class>
    static std::false_type check(...);
    using type = decltype(check<T>(0));

public:
    /// `true` if `T` meets the requirements.
    static bool const value =
        has_get_io_service<T>::value && type::value;
};
static_assert(! is_AsyncWriteStream<int>::value, "");

// http://www.boost.org/doc/libs/1_60_0/doc/html/boost_asio/reference/SyncReadStream.html
//
/// Determine if `T` meets the requirements of `SyncReadStream`.
template<class T>
class is_SyncReadStream
{
    using error_code =
        boost::system::error_code;

    template<class U, class R = std::is_same<decltype(
        std::declval<U>().read_some(
            std::declval<concept::MutableBufferSequence>())),
                std::size_t>>
    static R check1(int);
    template<class>
    static std::false_type check1(...);
    using type1 = decltype(check1<T>(0));

    template<class U, class R = std::is_same<decltype(
        std::declval<U>().read_some(
            std::declval<concept::MutableBufferSequence>(),
                std::declval<error_code&>())), std::size_t>>
    static R check2(int);
    template<class>
    static std::false_type check2(...);
    using type2 = decltype(check2<T>(0));

public:
    /// `true` if `T` meets the requirements.
    static bool const value =
        type1::value && type2::value;
};
static_assert(! is_SyncReadStream<int>::value, "");

// http://www.boost.org/doc/libs/1_60_0/doc/html/boost_asio/reference/SyncWriteStream.html
//
/// Determine if `T` meets the requirements of `SyncWriterStream`.
template<class T>
class is_SyncWriteStream
{
    using error_code =
        boost::system::error_code;

    template<class U, class R = std::is_same<decltype(
        std::declval<U>().write_some(
            std::declval<concept::ConstBufferSequence>())),
                std::size_t>>
    static R check1(int);
    template<class>
    static std::false_type check1(...);
    using type1 = decltype(check1<T>(0));

    template<class U, class R = std::is_same<decltype(
        std::declval<U>().write_some(
            std::declval<concept::ConstBufferSequence>(),
                std::declval<error_code&>())), std::size_t>>
    static R check2(int);
    template<class>
    static std::false_type check2(...);
    using type2 = decltype(check2<T>(0));

public:
    /// `true` if `T` meets the requirements.
    static bool const value =
        type1::value && type2::value;
};
static_assert(! is_SyncWriteStream<int>::value, "");

/// Determine if `T` meets the requirements of `SyncStream`.
template<class T>
struct is_SyncStream
{
    /// `true` if `T` meets the requirements.
    static bool const value =
        is_SyncReadStream<T>::value &&
        is_SyncWriteStream<T>::value;
};

/// Determine if `T` meets the requirements of `SyncStream`.
template<class T>
struct is_AsyncStream
{
    /// `true` if `T` meets the requirements.
    static bool const value =
        is_AsyncReadStream<T>::value &&
        is_AsyncWriteStream<T>::value;
};

/// Determine if `T` meets the requirements of `Streambuf`.
template<class T>
class is_Streambuf
{
    template<class U, class R = std::integral_constant<
        bool, is_MutableBufferSequence<decltype(
            std::declval<U>().prepare(1))>::value>>
    static R check1(int);
    template<class>
    static std::false_type check1(...);
    using type1 = decltype(check1<T>(0));

    template<class U, class R = std::integral_constant<
        bool, is_ConstBufferSequence<decltype(
            std::declval<U>().data())>::value>>
    static R check2(int);
    template<class>
    static std::false_type check2(...);
    using type2 = decltype(check2<T>(0));

    template<class U, class R = decltype(
        std::declval<U>().commit(1), std::true_type{})>
    static R check3(int);
    template<class>
    static std::false_type check3(...);
    using type3 = decltype(check3<T>(0));

    template<class U, class R = decltype(
        std::declval<U>().consume(1), std::true_type{})>
    static R check4(int);
    template<class>
    static std::false_type check4(...);
    using type4 = decltype(check4<T>(0));

    template<class U, class R = std::is_same<decltype(
        std::declval<U>().size()), std::size_t>>
    static R check5(int);
    template<class>
    static std::false_type check5(...);
    using type5 = decltype(check5<T>(0));

public:
    /// `true` if `T` meets the requirements.
    static bool const value =
        type1::value && type2::value &&
        type3::value && type4::value &&
        type5::value;
};

#if ! GENERATING_DOCS

/// Determine if `T` meets the requirements of `CompletionHandler`.
template<class T, class Signature>
using is_Handler = std::integral_constant<bool,
    std::is_copy_constructible<typename std::decay<T>::type>::value &&
        detail::is_call_possible<T, Signature>::value>;

#endif

} // beast

#endif
