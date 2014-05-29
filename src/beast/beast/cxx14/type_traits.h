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

#ifndef BEAST_CXX14_TYPE_TRAITS_H_INCLUDED
#define BEAST_CXX14_TYPE_TRAITS_H_INCLUDED

#include <beast/cxx14/config.h>

#include <tuple>
#include <type_traits>
#include <utility>

namespace std {

// Ideas from Howard Hinnant
//
// Specializations of is_constructible for pair and tuple which
// work around an apparent defect in the standard that causes well
// formed expressions involving pairs or tuples of non default-constructible
// types to generate compile errors.
//
template <class T, class U>
struct is_constructible <pair <T, U>>
    : integral_constant <bool,
        is_default_constructible <T>::value &&
        is_default_constructible <U>::value>
{
};

namespace detail {

template <bool...>
struct compile_time_all;

template <>
struct compile_time_all <>
{
    static const bool value = true;
};

template <bool Arg0, bool ... Argn>
struct compile_time_all <Arg0, Argn...>
{
    static const bool value =
        Arg0 && compile_time_all <Argn...>::value;
};

}

template <class ...T>
struct is_constructible <tuple <T...>>
    : integral_constant <bool,
        detail::compile_time_all <
            is_default_constructible <T>::value...>::value>
{
};

//------------------------------------------------------------------------------

#if ! BEAST_NO_CXX14_COMPATIBILITY

// From http://llvm.org/svn/llvm-project/libcxx/trunk/include/type_traits

// const-volatile modifications:
template <class T>
using remove_const_t    = typename remove_const<T>::type;  // C++14
template <class T>
using remove_volatile_t = typename remove_volatile<T>::type;  // C++14
template <class T>
using remove_cv_t       = typename remove_cv<T>::type;  // C++14
template <class T>
using add_const_t       = typename add_const<T>::type;  // C++14
template <class T>
using add_volatile_t    = typename add_volatile<T>::type;  // C++14
template <class T>
using add_cv_t          = typename add_cv<T>::type;  // C++14
  
// reference modifications:
template <class T>
using remove_reference_t     = typename remove_reference<T>::type;  // C++14
template <class T>
using add_lvalue_reference_t = typename add_lvalue_reference<T>::type;  // C++14
template <class T>
using add_rvalue_reference_t = typename add_rvalue_reference<T>::type;  // C++14
  
// sign modifications:
template <class T>
using make_signed_t   = typename make_signed<T>::type;  // C++14
template <class T>
using make_unsigned_t = typename make_unsigned<T>::type;  // C++14
  
// array modifications:
template <class T>
using remove_extent_t      = typename remove_extent<T>::type;  // C++14
template <class T>
using remove_all_extents_t = typename remove_all_extents<T>::type;  // C++14

// pointer modifications:
template <class T>
using remove_pointer_t = typename remove_pointer<T>::type;  // C++14
template <class T>
using add_pointer_t    = typename add_pointer<T>::type;  // C++14

// other transformations:

#if 0
// This is not easy to implement in C++11
template <size_t Len, std::size_t Align=std::alignment_of<max_align_t>::value>
using aligned_storage_t = typename aligned_storage<Len,Align>::type;  // C++14
template <std::size_t Len, class... Types>
using aligned_union_t   = typename aligned_union<Len,Types...>::type;  // C++14
#endif

template <class T>
using decay_t           = typename decay<T>::type;  // C++14
template <bool b, class T=void>
using enable_if_t       = typename enable_if<b,T>::type;  // C++14
template <bool b, class T, class F>
using conditional_t     = typename conditional<b,T,F>::type;  // C++14
template <class... T>
using common_type_t     = typename common_type<T...>::type;  // C++14
template <class T>
using underlying_type_t = typename underlying_type<T>::type;  // C++14
template <class F, class... ArgTypes>
using result_of_t       = typename result_of<F(ArgTypes...)>::type;  // C++14

#endif

}

#endif
