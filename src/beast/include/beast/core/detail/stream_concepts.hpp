//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_DETAIL_STREAM_CONCEPTS_HPP
#define BEAST_DETAIL_STREAM_CONCEPTS_HPP

#include <beast/core/buffer_concepts.hpp>
#include <beast/core/error.hpp>
#include <boost/asio/io_service.hpp>
#include <type_traits>
#include <utility>

namespace beast {
namespace detail {

// Types that meet the requirements,
// for use with std::declval only.
struct StreamHandler
{
    StreamHandler(StreamHandler const&) = default;
    void operator()(error_code ec, std::size_t);
};
using ReadHandler = StreamHandler;
using WriteHandler = StreamHandler;

template<class T>
class has_get_io_service
{
    template<class U, class R = typename std::is_same<
        decltype(std::declval<U>().get_io_service()),
            boost::asio::io_service&>>
    static R check(int);
    template<class>
    static std::false_type check(...);
public:
    using type = decltype(check<T>(0));
};

template<class T>
class is_AsyncReadStream
{
    template<class U, class R = decltype(
        std::declval<U>().async_read_some(
            std::declval<MutableBufferSequence>(),
                std::declval<ReadHandler>()),
                    std::true_type{})>
    static R check(int);
    template<class>
    static std::false_type check(...);
    using type1 = decltype(check<T>(0));
public:
    using type = std::integral_constant<bool,
        type1::value &&
        has_get_io_service<T>::type::value>;
};

template<class T>
class is_AsyncWriteStream
{
    template<class U, class R = decltype(
        std::declval<U>().async_write_some(
            std::declval<ConstBufferSequence>(),
                std::declval<WriteHandler>()),
                    std::true_type{})>
    static R check(int);
    template<class>
    static std::false_type check(...);
    using type1 = decltype(check<T>(0));
public:
    using type = std::integral_constant<bool,
        type1::value &&
        has_get_io_service<T>::type::value>;
};

template<class T>
class is_SyncReadStream
{
    template<class U, class R = std::is_same<decltype(
        std::declval<U>().read_some(
            std::declval<MutableBufferSequence>())),
                std::size_t>>
    static R check1(int);
    template<class>
    static std::false_type check1(...);
    using type1 = decltype(check1<T>(0));

    template<class U, class R = std::is_same<decltype(
        std::declval<U>().read_some(
            std::declval<MutableBufferSequence>(),
                std::declval<error_code&>())), std::size_t>>
    static R check2(int);
    template<class>
    static std::false_type check2(...);
    using type2 = decltype(check2<T>(0));

public:
    using type = std::integral_constant<bool,
        type1::value && type2::value>;
};

template<class T>
class is_SyncWriteStream
{
    template<class U, class R = std::is_same<decltype(
        std::declval<U>().write_some(
            std::declval<ConstBufferSequence>())),
                std::size_t>>
    static R check1(int);
    template<class>
    static std::false_type check1(...);
    using type1 = decltype(check1<T>(0));

    template<class U, class R = std::is_same<decltype(
        std::declval<U>().write_some(
            std::declval<ConstBufferSequence>(),
                std::declval<error_code&>())), std::size_t>>
    static R check2(int);
    template<class>
    static std::false_type check2(...);
    using type2 = decltype(check2<T>(0));

public:
    using type = std::integral_constant<bool,
        type1::value && type2::value>;
};

} // detail
} // beast

#endif
