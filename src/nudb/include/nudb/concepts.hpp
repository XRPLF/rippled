//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NUDB_CONCEPTS_HPP
#define NUDB_CONCEPTS_HPP

#include <nudb/error.hpp>
#include <nudb/file.hpp>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace nudb {

namespace detail {

template<class T>
class check_is_File
{
    template<class U, class R =
        std::is_convertible<decltype(
            std::declval<U const>().is_open()),
            bool>>
    static R check1(int);
    template<class>
    static std::false_type check1(...);
    using type1 = decltype(check1<T>(0));

    template<class U, class R = decltype(
        std::declval<U>().close(),
            std::true_type{})>
    static R check2(int);
    template<class>
    static std::false_type check2(...);
    using type2 = decltype(check2<T>(0));

    template<class U, class R = decltype(
        std::declval<U>().create(
            std::declval<file_mode>(),
            std::declval<path_type>(),
            std::declval<error_code&>()),
                std::true_type{})>
    static R check3(int);
    template<class>
    static std::false_type check3(...);
    using type3 = decltype(check3<T>(0));

    template<class U, class R = decltype(
        std::declval<U>().open(
            std::declval<file_mode>(),
            std::declval<path_type>(),
            std::declval<error_code&>()),
                std::true_type{})>
    static R check4(int);
    template<class>
    static std::false_type check4(...);
    using type4 = decltype(check4<T>(0));

    template<class U, class R = decltype(
        U::erase(
            std::declval<path_type>(),
            std::declval<error_code&>()),
                std::true_type{})>
    static R check5(int);
    template<class>
    static std::false_type check5(...);
    using type5 = decltype(check5<T>(0));

    template<class U, class R =
        std::is_convertible<decltype(
            std::declval<U const>().size(
                std::declval<error_code&>())),
            std::uint64_t>>
    static R check6(int);
    template<class>
    static std::false_type check6(...);
    using type6 = decltype(check6<T>(0));

    template<class U, class R = decltype(
        std::declval<U>().read(
            std::declval<std::uint64_t>(),
            std::declval<void*>(),
            std::declval<std::size_t>(),
            std::declval<error_code&>()),
                std::true_type{})>
    static R check7(int);
    template<class>
    static std::false_type check7(...);
    using type7 = decltype(check7<T>(0));

    template<class U, class R = decltype(
        std::declval<U>().write(
            std::declval<std::uint64_t>(),
            std::declval<void const*>(),
            std::declval<std::size_t>(),
            std::declval<error_code&>()),
                std::true_type{})>
    static R check8(int);
    template<class>
    static std::false_type check8(...);
    using type8 = decltype(check8<T>(0));

    template<class U, class R = decltype(
        std::declval<U>().sync(
            std::declval<error_code&>()),
                std::true_type{})>
    static R check9(int);
    template<class>
    static std::false_type check9(...);
    using type9 = decltype(check9<T>(0));

    template<class U, class R = decltype(
        std::declval<U>().trunc(
            std::declval<std::uint64_t>(),
            std::declval<error_code&>()),
                std::true_type{})>
    static R check10(int);
    template<class>
    static std::false_type check10(...);
    using type10 = decltype(check10<T>(0));

public:
    using type = std::integral_constant<bool,
        std::is_move_constructible<T>::value &&
        type1::value && type2::value && type3::value &&
        type4::value && type5::value && type6::value &&
        type7::value && type8::value && type9::value &&
        type10::value
    >;
};

template<class T>
class check_is_Hasher
{
    template<class U, class R =
        std::is_constructible<U, std::uint64_t>>
    static R check1(int);
    template<class>
    static std::false_type check1(...);
    using type1 = decltype(check1<T>(0));

    template<class U, class R =
        std::is_convertible<decltype(
            std::declval<U const>().operator()(
                std::declval<void const*>(),
                std::declval<std::size_t>())),
            std::uint64_t>>
    static R check2(int);
    template<class>
    static std::false_type check2(...);
    using type2 = decltype(check2<T>(0));
public:
    using type = std::integral_constant<bool,
        type1::value && type2::value>;
};

template<class T>
class check_is_Progress
{
    template<class U, class R = decltype(
        std::declval<U>().operator()(
            std::declval<std::uint64_t>(),
            std::declval<std::uint64_t>()),
                std::true_type{})>
    static R check1(int);
    template<class>
    static std::false_type check1(...);
public:
    using type = decltype(check1<T>(0));
};

} // detail

/// Determine if `T` meets the requirements of @b `File`
template<class T>
#if GENERATING_DOCS
struct is_File : std::integral_constant<bool, ...>{};
#else
using is_File = typename detail::check_is_File<T>::type;
#endif


/// Determine if `T` meets the requirements of @b `Hasher`
template<class T>
#if GENERATING_DOCS
struct is_Hasher : std::integral_constant<bool, ...>{};
#else
using is_Hasher = typename detail::check_is_Hasher<T>::type;
#endif

/// Determine if `T` meets the requirements of @b `Progress`
template<class T>
#if GENERATING_DOCS
struct is_Progress : std::integral_constant<bool, ...>{};
#else
using is_Progress = typename detail::check_is_Progress<T>::type;
#endif

} // nudb

#endif
