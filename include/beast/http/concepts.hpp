//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_TYPE_CHECK_HPP
#define BEAST_HTTP_TYPE_CHECK_HPP

#include <beast/core/error.hpp>
#include <boost/asio/buffer.hpp>
#include <type_traits>
#include <utility>

namespace beast {
namespace http {

namespace detail {

template<class T>
class has_value_type
{
    template<class U, class R =
        typename U::value_type>
    static std::true_type check(int);
    template<class>
    static std::false_type check(...);
    using type = decltype(check<T>(0));
public:
    static bool constexpr value = type::value;
};

template<class T, bool B = has_value_type<T>::value>
struct extract_value_type
{
    using type = void;
};

template<class T>
struct extract_value_type<T, true>
{
    using type = typename T::value_type;
};

template<class T>
class has_reader
{
    template<class U, class R =
        typename U::reader>
    static std::true_type check(int);
    template<class>
    static std::false_type check(...);
public:
    using type = decltype(check<T>(0));
};

template<class T>
class has_writer
{
    template<class U, class R =
        typename U::writer>
    static std::true_type check(int);
    template<class>
    static std::false_type check(...);
public:
    using type = decltype(check<T>(0));
};

template<class T>
struct is_Body
{
    using type = std::integral_constant<bool,
        has_value_type<T>::value &&
        std::is_default_constructible<
            typename extract_value_type<T>::type>::value
    >;
};

template<class T>
class is_Parser
{
    template<class U, class R =
        std::is_convertible<decltype(
            std::declval<U>().complete()),
            bool>>
    static R check1(int);
    template<class>
    static std::false_type check1(...);
    using type1 = decltype(check1<T>(0));

    template<class U, class R =
        std::is_convertible<decltype(
            std::declval<U>().write(
                std::declval<boost::asio::const_buffer const&>(),
                std::declval<error_code&>())),
            std::size_t>>
    static R check2(int);
    template<class>
    static std::false_type check2(...);
    using type2 = decltype(check2<T>(0));

    template<class U, class R = decltype(
        std::declval<U>().write_eof(std::declval<error_code&>()),
            std::true_type{})>
    static R check3(int);
    template<class>
    static std::false_type check3(...);
    using type3 = decltype(check3<T>(0));

public:
    using type = std::integral_constant<bool,
        type1::value &&
        type2::value &&
        type3::value
    >;
};

} // detail

/// Determine if `T` meets the requirements of `Body`.
template<class T>
#if GENERATING_DOCS
struct is_Body : std::integral_constant<bool, ...>{};
#else
using is_Body = typename detail::is_Body<T>::type;
#endif

/// Determine if `T` meets the requirements of `ReadableBody`.
template<class T>
#if GENERATING_DOCS
struct is_ReadableBody : std::integral_constant<bool, ...>{};
#else
using is_ReadableBody = typename detail::has_reader<T>::type;
#endif

/// Determine if `T` meets the requirements of `WritableBody`.
template<class T>
#if GENERATING_DOCS
struct is_WritableBody : std::integral_constant<bool, ...>{};
#else
using is_WritableBody = typename detail::has_writer<T>::type;
#endif

/// Determine if `T` meets the requirements of `Parser`.
template<class T>
#if GENERATING_DOCS
struct is_Parser : std::integral_constant<bool, ...>{};
#else
using is_Parser = typename detail::is_Parser<T>::type;
#endif

} // http
} // beast

#endif
