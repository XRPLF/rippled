//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_TYPE_CHECK_HPP
#define BEAST_HTTP_TYPE_CHECK_HPP

#include <beast/core/error.hpp>
#include <beast/core/detail/type_traits.hpp>
#include <beast/http/resume_context.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/logic/tribool.hpp>
#include <type_traits>
#include <utility>

namespace beast {
namespace http {

namespace detail {

struct write_function
{
    template<class ConstBufferSequence>
    void
    operator()(ConstBufferSequence const&);
};

template<class T, class = beast::detail::void_t<>>
struct has_value_type : std::false_type {};

template<class T>
struct has_value_type<T, beast::detail::void_t<
    typename T::value_type
        > > : std::true_type {};

template<class T, class = beast::detail::void_t<>>
struct has_content_length : std::false_type {};

template<class T>
struct has_content_length<T, beast::detail::void_t<decltype(
    std::declval<T>().content_length()
        )> > : std::true_type
{
    static_assert(std::is_convertible<
        decltype(std::declval<T>().content_length()),
            std::uint64_t>::value,
        "Writer::content_length requirements not met");
};

#if 0
template<class T, class M, class = beast::detail::void_t<>>
struct is_Writer : std::false_type {};

template<class T, class M>
struct is_Writer<T, M, beast::detail::void_t<decltype(
    std::declval<T>().init(
        std::declval<error_code&>())
    // VFALCO This is unfortunate, we have to provide the template
    //        argument type because this is not a deduced context?
    //
    ,std::declval<T>().template write<detail::write_function>(
        std::declval<resume_context>(),
        std::declval<error_code&>(),
        std::declval<detail::write_function>())
            )> > : std::integral_constant<bool,
    std::is_nothrow_constructible<T, M const&>::value &&
    std::is_convertible<decltype(
        std::declval<T>().template write<detail::write_function>(
            std::declval<resume_context>(),
            std::declval<error_code&>(),
            std::declval<detail::write_function>())),
                boost::tribool>::value
        >
{
    static_assert(std::is_same<
        typename M::body_type::writer, T>::value,
            "Mismatched writer and message");
};

#else

template<class T, class M>
class is_Writer
{
    template<class U, class R = decltype(
        std::declval<U>().init(std::declval<error_code&>()),
            std::true_type{})>
    static R check1(int);
    template<class>
    static std::false_type check1(...);
    using type1 = decltype(check1<T>(0));

    // VFALCO This is unfortunate, we have to provide the template
    //        argument type because this is not a deduced context?
    //
    template<class U, class R =
        std::is_convertible<decltype(
            std::declval<U>().template write<detail::write_function>(
                std::declval<resume_context>(),
                std::declval<error_code&>(),
                std::declval<detail::write_function>()))
            , boost::tribool>>
    static R check2(int);
    template<class>
    static std::false_type check2(...);
    using type2 = decltype(check2<T>(0));

public:
    static_assert(std::is_same<
        typename M::body_type::writer, T>::value,
            "Mismatched writer and message");

    using type = std::integral_constant<bool,
        std::is_nothrow_constructible<T, M const&>::value
        && type1::value
        && type2::value
    >;
};

#endif

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
                std::declval<boost::asio::const_buffers_1 const&>(),
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

/// Determine if `T` meets the requirements of @b Body.
template<class T>
#if GENERATING_DOCS
struct is_Body : std::integral_constant<bool, ...>{};
#else
using is_Body = detail::has_value_type<T>;
#endif

/** Determine if a @b Body has a nested type `reader`.

    @tparam T The type to check, which must meet the
    requirements of @b Body.
*/
#if GENERATING_DOCS
template<class T>
struct has_reader : std::integral_constant<bool, ...>{};
#else
template<class T, class = beast::detail::void_t<>>
struct has_reader : std::false_type {};

template<class T>
struct has_reader<T, beast::detail::void_t<
    typename T::reader
        > > : std::true_type {};
#endif

/** Determine if a @b Body has a nested type `writer`.

    @tparam T The type to check, which must meet the
    requirements of @b Body.
*/
#if GENERATING_DOCS
template<class T>
struct has_writer : std::integral_constant<bool, ...>{};
#else
template<class T, class = beast::detail::void_t<>>
struct has_writer : std::false_type {};

template<class T>
struct has_writer<T, beast::detail::void_t<
    typename T::writer
        > > : std::true_type {};
#endif

/** Determine if `T` meets the requirements of @b Reader for `M`.

    @tparam T The type to test.

    @tparam M The message type to test with, which must be of
    type `message`.
*/
#if GENERATING_DOCS
template<class T, class M>
struct is_Reader : std::integral_constant<bool, ...> {};
#else
template<class T, class M, class = beast::detail::void_t<>>
struct is_Reader : std::false_type {};

template<class T, class M>
struct is_Reader<T, M, beast::detail::void_t<decltype(
    std::declval<T>().init(
        std::declval<error_code&>()),
    std::declval<T>().write(
        std::declval<void const*>(),
        std::declval<std::size_t>(),
        std::declval<error_code&>())
            )> > : std::integral_constant<bool,
    std::is_nothrow_constructible<T, M&>::value
        >
{
    static_assert(std::is_same<
        typename M::body_type::reader, T>::value,
            "Mismatched reader and message");
};
#endif

/** Determine if `T` meets the requirements of @b Writer for `M`.

    @tparam T The type to test.

    @tparam M The message type to test with, which must be of
    type `message`.
*/
template<class T, class M>
#if GENERATING_DOCS
struct is_Writer : std::integral_constant<bool, ...> {};
#else
using is_Writer = typename detail::is_Writer<T, M>::type;
#endif

/// Determine if `T` meets the requirements of @b Parser.
template<class T>
#if GENERATING_DOCS
struct is_Parser : std::integral_constant<bool, ...>{};
#else
using is_Parser = typename detail::is_Parser<T>::type;
#endif

} // http
} // beast

#endif
